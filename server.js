import http from "node:http";
import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const PORT = Number(process.env.PORT || 5177);
const BIN = path.join(__dirname, "linkedlist_cli");

function send(res, status, headers, body) {
  res.writeHead(status, headers);
  res.end(body);
}

function contentTypeFor(p) {
  if (p.endsWith(".html")) return "text/html; charset=utf-8";
  if (p.endsWith(".css")) return "text/css; charset=utf-8";
  if (p.endsWith(".js")) return "text/javascript; charset=utf-8";
  if (p.endsWith(".json")) return "application/json; charset=utf-8";
  return "application/octet-stream";
}

function readJsonBody(req) {
  return new Promise((resolve, reject) => {
    let data = "";
    req.on("data", (chunk) => (data += chunk));
    req.on("end", () => {
      try {
        resolve(JSON.parse(data || "{}"));
      } catch (e) {
        reject(e);
      }
    });
  });
}

function runCpp(payload) {
  return new Promise((resolve, reject) => {
    if (!fs.existsSync(BIN)) {
      reject(
        new Error(
          "C++ binary not found. Compile it first: g++ -std=c++17 -O2 main.cpp -o linkedlist_cli"
        )
      );
      return;
    }

    const child = spawn(BIN, [], { cwd: __dirname });
    let out = "";
    let err = "";
    child.stdout.on("data", (d) => (out += d.toString()));
    child.stderr.on("data", (d) => (err += d.toString()));
    child.on("error", (e) => reject(e));
    child.on("close", (code) => {
      if (code !== 0) {
        reject(new Error(err || `C++ exited with code ${code}`));
        return;
      }
      try {
        resolve(JSON.parse(out));
      } catch (e) {
        reject(new Error("C++ did not output valid JSON.\n" + out));
      }
    });

    child.stdin.write(JSON.stringify(payload || {}));
    child.stdin.end();
  });
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);

  if (url.pathname === "/api/operate") {
    if (req.method !== "POST") {
      send(res, 405, { "Content-Type": "text/plain; charset=utf-8" }, "Method Not Allowed");
      return;
    }
    try {
      const body = await readJsonBody(req);
      const result = await runCpp(body);
      send(res, 200, { "Content-Type": "application/json; charset=utf-8" }, JSON.stringify(result));
    } catch (e) {
      send(res, 500, { "Content-Type": "text/plain; charset=utf-8" }, String(e?.message || e));
    }
    return;
  }

  // Static files
  let filePath = url.pathname === "/" ? "/index.html" : url.pathname;
  filePath = path.normalize(filePath).replace(/^(\.\.[/\\])+/, "");
  const abs = path.join(__dirname, filePath);

  if (!abs.startsWith(__dirname)) {
    send(res, 403, { "Content-Type": "text/plain; charset=utf-8" }, "Forbidden");
    return;
  }

  fs.readFile(abs, (err, data) => {
    if (err) {
      send(res, 404, { "Content-Type": "text/plain; charset=utf-8" }, "Not Found");
      return;
    }
    send(res, 200, { "Content-Type": contentTypeFor(abs) }, data);
  });
});

server.listen(PORT, () => {
  console.log(`Linked List Visualizer running at http://localhost:${PORT}`);
  console.log(`(Make sure you've compiled C++ binary: linkedlist_cli)`);
});

