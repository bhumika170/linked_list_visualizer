const $ = (id) => document.getElementById(id);

const els = {
  listType: $("listType"),
  valueInput: $("valueInput"),
  posInput: $("posInput"),
  fileInput: $("fileInput"),
  listRow: $("listRow"),
  message: $("message"),
  pillType: $("pillType"),
  pillComplexity: $("pillComplexity"),
};

let currentState = null;
let busy = false;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function setBusy(on) {
  busy = on;
  document.querySelectorAll("button.btn").forEach((b) => (b.disabled = on));
}

function parseNumberOrNull(s) {
  if (s === "" || s === null || s === undefined) return null;
  const n = Number(s);
  return Number.isFinite(n) ? n : null;
}

function messageClassFor(text) {
  const t = (text || "").toLowerCase();
  if (t.includes("failed") || t.includes("unknown")) return "bad";
  if (t.includes("not found")) return "warn";
  if (t.includes("insert") || t.includes("deleted") || t.includes("reversed") || t.includes("loaded") || t.includes("saved") || t.includes("undid")) return "good";
  return "";
}

function arrowElement(type) {
  const el = document.createElement("div");
  el.className = "arrow" + (type === "doubly" ? " double" : "");

  if (type === "doubly") {
    const head2 = document.createElement("div");
    head2.className = "head2";
    const shaft = document.createElement("div");
    shaft.className = "shaft";
    const head = document.createElement("div");
    head.className = "head";
    el.appendChild(head2);
    el.appendChild(shaft);
    el.appendChild(head);
    return el;
  }

  const shaft = document.createElement("div");
  shaft.className = "shaft";
  const head = document.createElement("div");
  head.className = "head";
  el.appendChild(shaft);
  el.appendChild(head);
  return el;
}

function nodeElement(node, idx, headIdx, tailIdx) {
  const box = document.createElement("div");
  box.className = "node";
  box.dataset.index = String(idx);

  const header = document.createElement("div");
  header.className = "nodeHeader";

  const val = document.createElement("div");
  val.className = "nodeVal";
  val.textContent = String(node.value);

  const addr = document.createElement("div");
  addr.className = "nodeAddr";
  addr.textContent = node.address;

  header.appendChild(val);
  header.appendChild(addr);
  box.appendChild(header);

  const badges = document.createElement("div");
  badges.className = "badgeRow";

  if (idx === headIdx) {
    const b = document.createElement("div");
    b.className = "badge head";
    b.textContent = "HEAD";
    badges.appendChild(b);
  }
  if (idx === tailIdx) {
    const b = document.createElement("div");
    b.className = "badge tail";
    b.textContent = "TAIL";
    badges.appendChild(b);
  }

  box.appendChild(badges);
  return box;
}

function render(state, { animateInIndex = null, reverseFlip = false } = {}) {
  currentState = state;
  els.listRow.innerHTML = "";

  if (!state) return;

  const type = state.type || "singly";
  const nodes = Array.isArray(state.nodes) ? state.nodes : [];
  const headIdx = Number.isFinite(state.head) ? state.head : -1;
  const tailIdx = Number.isFinite(state.tail) ? state.tail : -1;

  els.pillType.textContent = `type: ${type}`;
  els.pillComplexity.textContent = `complexity: ${state?.meta?.complexity ?? "—"}`;

  if (reverseFlip) els.listRow.classList.add("reverseFlip");
  else els.listRow.classList.remove("reverseFlip");

  nodes.forEach((n, i) => {
    const nodeEl = nodeElement(n, i, headIdx, tailIdx);
    if (animateInIndex === i) nodeEl.classList.add("anim-in");
    els.listRow.appendChild(nodeEl);

    const isLast = i === nodes.length - 1;
    if (!isLast) {
      els.listRow.appendChild(arrowElement(type));
    }
  });

  if (nodes.length === 0) {
    const nullBox = document.createElement("div");
    nullBox.className = "nullBox";
    nullBox.textContent = "EMPTY";
    els.listRow.appendChild(nullBox);
    return;
  }

  if (type === "circular") {
    const loop = document.createElement("div");
    loop.className = "nullBox";
    loop.textContent = "↩ tail → head (circular)";
    els.listRow.appendChild(loop);
  } else {
    const nullBox = document.createElement("div");
    nullBox.className = "nullBox";
    nullBox.textContent = "NULL";
    els.listRow.appendChild(nullBox);
  }
}

function showMessage(text) {
  els.message.className = "message " + messageClassFor(text);
  els.message.innerHTML = `<strong>${escapeHtml(text || "")}</strong>`;
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => {
    switch (c) {
      case "&":
        return "&amp;";
      case "<":
        return "&lt;";
      case ">":
        return "&gt;";
      case '"':
        return "&quot;";
      case "'":
        return "&#039;";
      default:
        return c;
    }
  });
}

async function highlightSteps(steps, { foundIndex = -1 } = {}) {
  const unique = Array.from(new Set((steps || []).filter((x) => Number.isFinite(x))));
  for (const idx of unique) {
    const el = els.listRow.querySelector(`.node[data-index="${idx}"]`);
    if (!el) continue;
    el.classList.add("current");
    await sleep(320);
    el.classList.remove("current");
  }
  if (foundIndex !== -1 && foundIndex !== null && foundIndex !== undefined) {
    const found = els.listRow.querySelector(`.node[data-index="${foundIndex}"]`);
    if (found) {
      found.classList.add("found");
      await sleep(650);
      found.classList.remove("found");
    }
  }
}

async function animateDelete(index) {
  const el = els.listRow.querySelector(`.node[data-index="${index}"]`);
  if (!el) return;
  el.classList.add("anim-out");
  await sleep(230);
}

function diffFirstChangedIndex(prev, next) {
  const a = (prev?.nodes || []).map((n) => n.value);
  const b = (next?.nodes || []).map((n) => n.value);
  const min = Math.min(a.length, b.length);
  for (let i = 0; i < min; i++) if (a[i] !== b[i]) return i;
  if (a.length !== b.length) return min;
  return -1;
}

async function callCpp(op) {
  const type = els.listType.value;
  const value = parseNumberOrNull(els.valueInput.value);
  const position = parseNumberOrNull(els.posInput.value);
  const file = els.fileInput.value || "sample_state.json";

  const payload = { op, type, value, position, file };
  const res = await fetch("/api/operate", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });

  if (!res.ok) {
    const t = await res.text().catch(() => "");
    throw new Error(t || `HTTP ${res.status}`);
  }
  return await res.json();
}

async function runOperation(op) {
  if (busy) return;
  setBusy(true);
  try {
    const prev = currentState;

    // Pre-animation: for delete by position/begin/end, fade the target node first.
    if (op.startsWith("delete") && prev && prev.nodes && prev.nodes.length) {
      let idx = null;
      if (op === "deleteAtBeginning") idx = 0;
      if (op === "deleteAtEnd") idx = prev.nodes.length - 1;
      if (op === "deleteAtPosition") idx = parseNumberOrNull(els.posInput.value);
      if (idx !== null && idx >= 0 && idx < prev.nodes.length) await animateDelete(idx);
    }

    const next = await callCpp(op);

    const meta = next?.meta || {};
    showMessage(meta.message || "Done.");

    // Render + animations
    const changedIndex = diffFirstChangedIndex(prev, next);
    const inserted = (next?.nodes?.length || 0) > (prev?.nodes?.length || 0);
    const reversed = op === "reverse";

    render(next, {
      animateInIndex: inserted ? Math.max(0, changedIndex) : null,
      reverseFlip: reversed,
    });

    // Traversal animation
    if (Array.isArray(meta.steps) && meta.steps.length) {
      await highlightSteps(meta.steps, { foundIndex: meta.searchResultIndex ?? -1 });
    } else if (Number.isFinite(meta.highlightIndex) && meta.highlightIndex >= 0) {
      await highlightSteps([meta.highlightIndex], { foundIndex: meta.searchResultIndex ?? -1 });
    }
  } catch (e) {
    showMessage(`Error: ${e.message || e}`);
  } finally {
    setBusy(false);
  }
}

function wireButtons() {
  document.querySelectorAll("button.btn[data-op]").forEach((btn) => {
    btn.addEventListener("click", () => runOperation(btn.dataset.op));
  });
  els.listType.addEventListener("change", () => runOperation("noop"));
}

async function boot() {
  wireButtons();
  setBusy(true);
  try {
    const state = await callCpp("noop");
    render(state);
    showMessage(state?.meta?.message || "Ready.");
  } catch (e) {
    showMessage(
      "Could not connect to local server. Start it with: `node server.js` (and compile C++ first)."
    );
  } finally {
    setBusy(false);
  }
}

function bootTheoryReveal() {
  const targets = Array.from(document.querySelectorAll(".reveal"));
  if (!targets.length) return;

  if (!("IntersectionObserver" in window)) {
    targets.forEach((el) => el.classList.add("is-visible"));
    return;
  }

  const io = new IntersectionObserver(
    (entries) => {
      for (const entry of entries) {
        if (!entry.isIntersecting) continue;
        entry.target.classList.add("is-visible");
        io.unobserve(entry.target);
      }
    },
    { threshold: 0.12, rootMargin: "40px 0px -10% 0px" }
  );

  targets.forEach((el) => io.observe(el));
}

boot();
bootTheoryReveal();

