#!/usr/bin/env python3
"""Run the organism zoo and write the report.

Every organism (0 = the example build, 1..N generated) runs under three
conditions: the full engine, the same engine with learned reflexes off, and
a no-engine random walk using the organism's own locomotion. Results go to
zoo-results.json (everything) and ZOO.md (the report), and a summary for
the site to ../../docs/sim/zoo.json.

    python3 zoo.py --organisms 50 --runs 3 --lives 4
"""

import argparse
import json
import os
import statistics as st
import subprocess
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.join(HERE, "emergent-sim")
CONDITIONS = ["full", "no_learning", "random_walk"]


def run(organism, condition, runs, lives):
    out = subprocess.run(
        [SIM, "--organism", str(organism), "--condition", condition, "--runs", str(runs), "--lives", str(lives), "--json"],
        capture_output=True, text=True, check=True,
    ).stdout
    return organism, condition, json.loads(out)


def mean(xs):
    xs = [x for x in xs if x is not None]
    return st.mean(xs) if xs else None


def summarize(res):
    """Per condition: self-sufficiency (lifespan / idle endurance), per-life curve, behavior."""
    idle = res["idle_endurance_h"]
    cap = res["hours"]
    out = {}
    for cond, data in res["conditions"].items():
        lives = [life for run in data["runs"] for life in run]
        n_lives = len(data["runs"][0])
        curve = [mean(run[i]["lifespan_h"] / idle for run in data["runs"]) for i in range(n_lives)]
        out[cond] = {
            "self_sufficiency": mean(l["lifespan_h"] / idle for l in lives),
            "survived_cap": mean(0.0 if l["died"] else 1.0 for l in lives),
            "docked": mean(l["docked"] for l in lives),
            "charge_per_life": mean(l["charge_ah"] for l in lives) / res["organism"]["capacity_ah"],
            "dockings": mean(l["dockings"] for l in lives),
            "coverage": mean(l["coverage"] for l in lives),
            "speed": mean(l["speed"] for l in lives),
            "dist_hungry": mean(l["dist_hungry"] for l in lives),
            "dist_sated": mean(l["dist_sated"] for l in lives),
            "entropy": mean(l["entropy"] for l in lives),
            "curve": curve,
        }
        if "reflexes" in data:
            out[cond]["reflexes"] = data["reflexes"]
    return {"cap_h": cap, "idle_h": idle, "conditions": out}


def verdict(s):
    full, base, rw = (s["conditions"][c] for c in CONDITIONS)
    gain = full["self_sufficiency"] - base["self_sufficiency"]
    if full["self_sufficiency"] >= 2.4:
        return "self-sufficient"
    if gain > 0.25:
        return "learned to feed"
    if gain > 0.08:
        return "some benefit"
    if gain < -0.08:
        return "reflexes hurt"
    return "no benefit"


def describe(p):
    senses = {}
    for s in p["sensors"]:
        senses[s["type"]] = senses.get(s["type"], 0) + 1
    sense_txt = ", ".join(f"{n}× {t}" if n > 1 else t for t, n in senses.items())
    outs = ", ".join(a["name"] for a in p["actuators"])
    return sense_txt, outs


def fmt(x, pct=False, d=2):
    if x is None:
        return "–"
    return f"{100 * x:.0f}%" if pct else f"{x:.{d}f}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--organisms", type=int, default=50)
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--lives", type=int, default=4)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    args = ap.parse_args()

    ids = list(range(0, args.organisms + 1))
    jobs = [(i, c) for i in ids for c in CONDITIONS]
    results = {}
    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for organism, cond, res in ex.map(lambda j: run(j[0], j[1], args.runs, args.lives), jobs):
            results.setdefault(organism, {"organism": res["organism"], "hours": res["hours"],
                                          "idle_endurance_h": res["idle_endurance_h"], "conditions": {}})
            results[organism]["conditions"].update(res["conditions"])
            print(f"organism {organism:2d} {cond:12s} done", flush=True)

    rows = []
    for i in ids:
        s = summarize(results[i])
        s["organism"] = results[i]["organism"]
        s["verdict"] = verdict(s)
        rows.append(s)

    with open(os.path.join(HERE, "zoo-results.json"), "w") as f:
        json.dump({"runs": args.runs, "lives": args.lives, "organisms": rows}, f, separators=(",", ":"))

    site = []
    for r in rows:
        p = r["organism"]
        site.append({"id": p["id"], "title": p["title"], "loco": p["loco"], "loco_desc": p["loco_desc"],
                     "temperament": p["temperament"], "cells": p["cells"], "capacity_ah": p["capacity_ah"],
                     "sensors": [{"name": s["name"], "type": s["type"], "part": s["part"]} for s in p["sensors"]],
                     "actuators": [{"name": a["name"], "role": a["role"], "part": a["part"]} for a in p["actuators"]],
                     "idle_h": r["idle_h"], "verdict": r["verdict"],
                     "results": {c: {k: v for k, v in r["conditions"][c].items()} for c in CONDITIONS}})
    os.makedirs(os.path.join(HERE, "..", "..", "docs", "sim"), exist_ok=True)
    with open(os.path.join(HERE, "..", "..", "docs", "sim", "zoo.json"), "w") as f:
        json.dump({"runs": args.runs, "lives": args.lives, "organisms": site}, f, separators=(",", ":"))

    write_report(rows, args)


def write_report(rows, args):
    full = [r["conditions"]["full"]["self_sufficiency"] for r in rows]
    base = [r["conditions"]["no_learning"]["self_sufficiency"] for r in rows]
    rw = [r["conditions"]["random_walk"]["self_sufficiency"] for r in rows]
    better = sum(1 for f, b in zip(full, base) if f - b > 0.08)
    worse = sum(1 for f, b in zip(full, base) if b - f > 0.08)
    by_loco = {}
    for r in rows:
        by_loco.setdefault(r["organism"]["loco_desc"], []).append(r)
    verdicts = {}
    for r in rows:
        verdicts[r["verdict"]] = verdicts.get(r["verdict"], 0) + 1

    L = []
    L.append("# Organism zoo\n")
    L.append("Generated by `python3 zoo.py --organisms %d --runs %d --lives %d` (%d organisms: the example build "
             "plus %d generated bodies). Each organism runs %d times, each run %d lives with memory and learned "
             "reflexes kept between lives, under three conditions: the full engine, the engine with learned "
             "reflexes off, and a random walk that uses the body's own locomotion with no engine at all.\n"
             % (len(rows) - 1, args.runs, args.lives, len(rows), len(rows) - 1, args.runs, args.lives))
    L.append("**Self-sufficiency** is lifespan divided by the body's idle endurance, how long it would last "
             "sitting perfectly still. Below 1.0 it spends energy faster than idling; above 1.0 it must have "
             "taken charge from the dock. Each life is capped at 2.5× idle endurance (10 h at most), so 2.5 "
             "means every life reached the cap.\n")
    L.append("## Summary\n")
    L.append(f"- Mean self-sufficiency: **full engine {st.mean(full):.2f}**, reflexes off {st.mean(base):.2f}, "
             f"random walk {st.mean(rw):.2f}.")
    L.append(f"- Learned reflexes helped {better} of {len(rows)} organisms by more than 0.08, and hurt {worse}.")
    L.append("- Verdicts: " + ", ".join(f"{k} {v}" for k, v in sorted(verdicts.items(), key=lambda kv: -kv[1])) + ".\n")
    L.append("| Locomotion | Organisms | Full | Reflexes off | Random walk | Helped |")
    L.append("|---|---|---|---|---|---|")
    for loco, rs in sorted(by_loco.items()):
        f_ = st.mean(r["conditions"]["full"]["self_sufficiency"] for r in rs)
        b_ = st.mean(r["conditions"]["no_learning"]["self_sufficiency"] for r in rs)
        w_ = st.mean(r["conditions"]["random_walk"]["self_sufficiency"] for r in rs)
        h_ = sum(1 for r in rs if r["conditions"]["full"]["self_sufficiency"] - r["conditions"]["no_learning"]["self_sufficiency"] > 0.08)
        L.append(f"| {loco} | {len(rs)} | {f_:.2f} | {b_:.2f} | {w_:.2f} | {h_}/{len(rs)} |")
    L.append("")
    L.append("## Organisms\n")
    L.append("Self-sufficiency per condition, then the full engine's per-life curve (life 1 → %d), then what it "
             "learned most strongly (context: sensor → actuator, weight) in its first run.\n" % args.lives)
    for r in rows:
        p = r["organism"]
        c = r["conditions"]
        sense_txt, outs = describe(p)
        L.append(f"### {p['title']}: {r['verdict']}\n")
        L.append(f"{p['loco_desc'].capitalize()}, radius {p['radius'] * 100:.0f} cm, {p['cells']}S LiPo "
                 f"{p['capacity_ah']:.2f} Ah (idle endurance {r['idle_h']:.1f} h). Temperament: {p['temperament']}.\n")
        L.append(f"- Senses: {sense_txt}")
        L.append(f"- Outputs: {outs}")
        L.append(f"- Self-sufficiency: full **{fmt(c['full']['self_sufficiency'])}**, reflexes off "
                 f"{fmt(c['no_reflexes']['self_sufficiency'])}, random walk {fmt(c['random_walk']['self_sufficiency'])}")
        L.append(f"- Full engine: docked {fmt(c['full']['docked'], True)} of the time, "
                 f"{fmt(c['full']['charge_per_life'], True)} of a battery taken per life, "
                 f"coverage {fmt(c['full']['coverage'], True)}, distance to station hungry/sated "
                 f"{fmt(c['full']['dist_hungry'])}/{fmt(c['full']['dist_sated'])} m")
        L.append("- Per life: " + " → ".join(fmt(x) for x in c["full"]["curve"]))
        ref = c["full"].get("reflexes", [])[:4]
        if ref:
            L.append("- Strongest reflexes: " + "; ".join(f"{w['context'].replace('h_', '')}: {w['from']} → {w['to']} {w['w']:+.2f}" for w in ref))
        L.append("")
    with open(os.path.join(HERE, "ZOO.md"), "w") as f:
        f.write("\n".join(L))
    print("\n".join(L[:14]))


if __name__ == "__main__":
    main()
