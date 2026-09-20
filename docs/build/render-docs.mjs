#!/usr/bin/env node
// Render docs/*.md → docs/*.html (+ PDF for the article), with mermaid
// diagrams pre-rendered to inline SVG so they display everywhere without
// client-side JavaScript. Single entry point used by CI and local runs so
// the two never drift.

import { copyFile, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { run } from "./run.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../..");
const RENDER_MERMAID = resolve(HERE, "render-mermaid.mjs");
const MD_TO_PDF = resolve(REPO, "node_modules/.bin/md-to-pdf");
const STYLESHEET = resolve(REPO, "docs/assets/article.css");
const PUPPETEER_ARGS = JSON.stringify({
  args: ["--no-sandbox", "--disable-setuid-sandbox"],
});

// md-to-pdf has no --dest flag: it always writes next to the input file,
// same basename, .html/.pdf extension. So we preprocess into a tmp dir named
// after the real target, then copy the result out.
async function render({ src, out_html, out_pdf }) {
  const tmp = await mkdtemp(join(tmpdir(), "docs-"));
  const preprocessed = join(tmp, "article.md");
  try {
    await run("node", [RENDER_MERMAID, src, preprocessed]);

    await run(MD_TO_PDF, [
      preprocessed,
      "--as-html",
      "--stylesheet", STYLESHEET,
      "--launch-options", PUPPETEER_ARGS,
    ]);
    await copyFile(join(tmp, "article.html"), out_html);

    if (out_pdf) {
      await run(MD_TO_PDF, [
        preprocessed,
        "--stylesheet", STYLESHEET,
        "--launch-options", PUPPETEER_ARGS,
      ]);
      await copyFile(join(tmp, "article.pdf"), out_pdf);
    }
    console.log(`rendered ${src} -> ${out_html}${out_pdf ? ` + ${out_pdf}` : ""}`);
  } finally {
    await rm(tmp, { recursive: true, force: true });
  }
}

// Splices a rendered page's body into index.html between two named marker
// comments, so the frontpage always carries the same content as the
// standalone page — one source (synth-behavior.md / roadmap.md), two
// presentations. Strips the page's own <h1> masthead and, optionally, a
// "back to project overview" link — both meaningless/circular once embedded
// in that same page; index.html supplies its own header/section-label
// instead.
async function embedFragmentIntoIndex(srcHtmlPath, indexHtmlPath, markerName, { stripBackLink = false } = {}) {
  const srcHtml = await readFile(srcHtmlPath, "utf8");
  const bodyMatch = srcHtml.match(/<body[^>]*>([\s\S]*)<\/body>/i);
  if (!bodyMatch) throw new Error(`could not find <body> in ${srcHtmlPath}`);

  let fragment = bodyMatch[1];
  fragment = fragment.replace(/^\s*<h1[^>]*>[\s\S]*?<\/h1>\s*/i, "");
  if (stripBackLink) {
    fragment = fragment.replace(/^\s*<p><a href="index\.html">[\s\S]*?<\/a><\/p>\s*/i, "");
  }
  fragment = fragment.trim();

  const indexHtml = await readFile(indexHtmlPath, "utf8");
  const markerRe = new RegExp(`<!--${markerName}-START-->[\\s\\S]*?<!--${markerName}-END-->`);
  if (!markerRe.test(indexHtml)) {
    throw new Error(`${markerName} markers not found in ${indexHtmlPath}`);
  }
  const updated = indexHtml.replace(
    markerRe,
    `<!--${markerName}-START-->\n${fragment}\n<!--${markerName}-END-->`
  );
  await writeFile(indexHtmlPath, updated);
  console.log(`embedded ${srcHtmlPath} -> ${indexHtmlPath}`);
}

const synthBehaviorHtml = resolve(REPO, "docs/synth-behavior.html");
const roadmapHtml = resolve(REPO, "docs/roadmap.html");
const indexHtml = resolve(REPO, "docs/index.html");

await render({
  src: resolve(REPO, "docs/synth-behavior.md"),
  out_html: synthBehaviorHtml,
  out_pdf: resolve(REPO, "docs/synth-behavior.pdf"),
});
await render({
  src: resolve(REPO, "docs/roadmap.md"),
  out_html: roadmapHtml,
});
await embedFragmentIntoIndex(synthBehaviorHtml, indexHtml, "ARTICLE-CONTENT", { stripBackLink: true });
await embedFragmentIntoIndex(roadmapHtml, indexHtml, "ROADMAP-CONTENT");
