// child_process.execFile (and its promisified form) resolves only once
// stdout/stderr *streams close* — not merely once the child process exits.
// If a grandchild (e.g. a headless Chrome instance Puppeteer spawns)
// inherits those file descriptors and doesn't fully let go of them, execFile
// hangs forever even though the direct child is long gone. This bit us
// running md-to-pdf here: it returned instantly from a shell, but hung
// indefinitely through execFile.
//
// spawn() + resolving on the child's own "exit" event sidesteps that
// entirely, and the timeout below is a hard backstop in case a command
// really does hang for an unrelated reason.

import { spawn } from "node:child_process";

export function run(cmd, args, { timeoutMs = 120_000 } = {}) {
  return new Promise((resolvePromise, reject) => {
    const child = spawn(cmd, args, { stdio: ["ignore", "pipe", "pipe"] });
    let stdout = "";
    let stderr = "";
    let settled = false;

    const timer = setTimeout(() => {
      if (settled) return;
      settled = true;
      child.kill("SIGKILL");
      reject(new Error(`${cmd} timed out after ${timeoutMs}ms\n${stderr}`));
    }, timeoutMs);

    child.stdout.on("data", (d) => (stdout += d));
    child.stderr.on("data", (d) => (stderr += d));

    child.on("error", (err) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      reject(err);
    });

    child.on("exit", (code) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      if (code === 0) {
        resolvePromise({ stdout, stderr });
      } else {
        reject(new Error(`${cmd} ${args.join(" ")} exited ${code}\n${stderr}`));
      }
    });
  });
}
