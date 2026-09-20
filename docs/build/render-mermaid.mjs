#!/usr/bin/env node
// Preprocess a Markdown file: pre-render every ```mermaid``` block into an
// inline <svg> via @mermaid-js/mermaid-cli (mmdc). The rewritten Markdown is
// safe to hand to any renderer (md-to-pdf, GitHub Pages, etc.) — no
// client-side JavaScript, no CDN dependency, identical output in HTML/PDF.
//
// Usage: render-mermaid.mjs <input.md> <output.md>

import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { run } from "./run.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const MMDC = resolve(HERE, "../../node_modules/.bin/mmdc");
const PUPPETEER_CONFIG = resolve(HERE, "puppeteer.json");

async function renderOne(source, tmp, index) {
  const inFile = join(tmp, `d${index}.mmd`);
  const outFile = join(tmp, `d${index}.svg`);
  await writeFile(inFile, source);
  await run(MMDC, [
    "-i", inFile,
    "-o", outFile,
    "-b", "transparent",
    "--puppeteerConfigFile", PUPPETEER_CONFIG,
  ]);
  let svg = await readFile(outFile, "utf8");

  // mmdc emits an XML declaration and a `<!DOCTYPE svg>` line; strip both so
  // the SVG embeds cleanly mid-document.
  svg = svg.replace(/^<\?xml[^?]*\?>\s*/i, "");
  svg = svg.replace(/^<!DOCTYPE[^>]*>\s*/i, "");

  // mmdc hardcodes "my-svg" as the id — used both as the root id="my-svg"
  // and as the scoping prefix for every rule in the embedded <style> block
  // (e.g. "#my-svg .node rect {...}") plus marker/clip-path url(#...) refs.
  // With several diagrams on one page, identical ids collide and a later
  // diagram's <style> silently reskins an earlier one. A blind attribute
  // strip breaks the internal "#my-svg" selectors instead (they'd reference
  // an id that no longer exists). Renaming the literal string everywhere —
  // attribute, selector, and url() refs alike — keeps each diagram self
  // -scoped and unique.
  svg = svg.split("my-svg").join(`mermaid-diagram-${index}`);

  return svg;
}

async function main() {
  const [, , input, output] = process.argv;
  if (!input || !output) {
    console.error("usage: render-mermaid.mjs <input.md> <output.md>");
    process.exit(2);
  }

  const src = await readFile(input, "utf8");
  const re = /^```mermaid\n([\s\S]*?)^```/gm;
  const tmp = await mkdtemp(join(tmpdir(), "mermaid-"));

  const jobs = [];
  let match;
  while ((match = re.exec(src)) !== null) {
    jobs.push({ full: match[0], body: match[1], idx: jobs.length });
  }

  if (jobs.length === 0) {
    await writeFile(output, src);
    return;
  }

  const svgs = await Promise.all(jobs.map((j) => renderOne(j.body, tmp, j.idx)));

  // Splice back in order; wrap each in a <figure> so the article stylesheet
  // can center/size them.
  let out = src;
  for (let i = 0; i < jobs.length; i++) {
    const html = `<figure class="mermaid">${svgs[i]}</figure>`;
    out = out.replace(jobs[i].full, html);
  }

  await writeFile(output, out);
  await rm(tmp, { recursive: true, force: true });
  console.log(`rendered ${jobs.length} mermaid diagram(s) → ${output}`);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
