"use strict";

const $ = (id) => document.getElementById(id);
const editor = $("source");
const highlightEl = $("highlight");
const gutter = $("gutter");
const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
const DURATION = reduceMotion ? 0 : 260;
const NODE_HEIGHT = 38;
const LABEL_LIMIT = 26;

const state = {
  data: null,
  diagnostics: [],
  asm: "",
  requestId: 0,
  collapsed: new Set(),
  showImplicit: false,
  horizontal: false,
  needsFit: true,
  mark: null,
  activeKey: null,
};

const KEYWORDS = new Set(("auto break case char const continue default do double else enum extern float for goto if inline int long register restrict return short signed sizeof static struct switch typedef union unsigned void volatile while bool _Bool").split(" "));

function escapeHtml(s) {
  return s.replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c]));
}

function tokenize(text) {
  const re = /\/\*[\s\S]*?(?:\*\/|$)|\/\/[^\n]*|^[ \t]*#[^\n]*|"(?:[^"\\\n]|\\[\s\S])*"?|'(?:[^'\\\n]|\\.)*'?|\b0[xX][0-9a-fA-F]+[uUlL]*\b|\b\d+\.?\d*(?:[eE][+-]?\d+)?[uUlLfF]*\b|\b[A-Za-z_]\w*\b/gm;
  const out = [];
  let m;
  while ((m = re.exec(text))) {
    const t = m[0];
    let cls = null;
    if (t.startsWith("/*") || t.startsWith("//")) cls = "t-com";
    else if (t.trimStart().startsWith("#")) cls = "t-pre";
    else if (t[0] === '"' || t[0] === "'") cls = "t-str";
    else if (/^\d/.test(t)) cls = "t-num";
    else if (KEYWORDS.has(t)) cls = "t-kw";
    if (cls) out.push({ start: m.index, end: m.index + t.length, cls });
  }
  return out;
}

function renderHighlight() {
  const text = editor.value;
  const tokens = tokenize(text);
  const mark = state.mark;
  const cuts = new Set([0, text.length]);
  tokens.forEach((t) => { cuts.add(t.start); cuts.add(t.end); });
  if (mark) { cuts.add(mark.from); cuts.add(mark.to); }
  const points = [...cuts].filter((p) => p <= text.length).sort((a, b) => a - b);
  let html = "";
  let ti = 0;
  for (let i = 0; i + 1 < points.length; i++) {
    const a = points[i];
    const b = points[i + 1];
    while (ti < tokens.length && tokens[ti].end <= a) ti++;
    const tok = ti < tokens.length && tokens[ti].start <= a ? tokens[ti] : null;
    let piece = escapeHtml(text.slice(a, b));
    if (tok) piece = `<span class="${tok.cls}">${piece}</span>`;
    if (mark && a >= mark.from && b <= mark.to) piece = `<mark>${piece}</mark>`;
    html += piece;
  }
  highlightEl.innerHTML = html + "\n";
  renderGutter();
  syncScroll();
}

function renderGutter() {
  const count = editor.value.split("\n").length;
  const severity = new Map();
  state.diagnostics.forEach((d) => {
    if (d.severity === "error") severity.set(d.line, "err");
    else if (!severity.has(d.line)) severity.set(d.line, "warn");
  });
  let html = "";
  for (let i = 1; i <= count; i++) {
    const cls = severity.get(i);
    html += `<div${cls ? ` class="${cls}"` : ""}>${i}</div>`;
  }
  gutter.innerHTML = html;
}

function syncScroll() {
  highlightEl.scrollTop = editor.scrollTop;
  highlightEl.scrollLeft = editor.scrollLeft;
  gutter.scrollTop = editor.scrollTop;
}

function byteColToIndex(lineText, col) {
  const target = Math.max(col - 1, 0);
  let bytes = 0;
  let i = 0;
  const encoder = new TextEncoder();
  for (const ch of lineText) {
    if (bytes >= target) break;
    bytes += encoder.encode(ch).length;
    i += ch.length;
  }
  return i;
}

function rangeToOffsets(node) {
  const lines = editor.value.split("\n");
  const start = (l) => {
    let o = 0;
    for (let i = 0; i < l - 1 && i < lines.length; i++) o += lines[i].length + 1;
    return o;
  };
  if (!node.line || node.line > lines.length) return null;
  const line = lines[node.line - 1];
  if (!node.col) return { from: start(node.line), to: start(node.line) + line.length };
  const eline = Math.min(node.eline || node.line, lines.length);
  return {
    from: start(node.line) + byteColToIndex(line, node.col),
    to: start(eline) + byteColToIndex(lines[eline - 1], node.ecol || node.col),
  };
}

function setMark(node) {
  state.mark = node ? rangeToOffsets(node) : null;
  renderHighlight();
}

function revealMark() {
  if (!state.mark) return;
  const line = editor.value.slice(0, state.mark.from).split("\n").length;
  const top = (line - 1) * 20;
  if (top < editor.scrollTop || top > editor.scrollTop + editor.clientHeight - 40) {
    editor.scrollTop = Math.max(0, top - editor.clientHeight / 2);
    syncScroll();
  }
}

const canvas = document.createElement("canvas").getContext("2d");

function textWidth(text, font) {
  canvas.font = font;
  return canvas.measureText(text).width;
}

function shorten(s) {
  return s.length > LABEL_LIMIT ? s.slice(0, LABEL_LIMIT - 1) + "…" : s;
}

function viewOf(node, showImplicit) {
  const children = [];
  (node.children || []).forEach((child) => {
    let c = child;
    if (!showImplicit) {
      while (c.kind === "ImplicitCast" && c.children.length === 1) {
        c = { ...c.children[0], role: child.role };
      }
    }
    children.push(viewOf(c, showImplicit));
  });
  return { ...node, children };
}

function assignKeys(node, key) {
  node.key = key;
  node.children.forEach((c, i) => assignKeys(c, `${key}/${i}:${c.kind}`));
}

function countNodes(n) {
  return 1 + (n.children || []).reduce((s, c) => s + countNodes(c), 0);
}

const svg = d3.select("#tree-svg");
const viewport = svg.append("g");
const linkLayer = viewport.append("g");
const roleLayer = viewport.append("g");
const nodeLayer = viewport.append("g");
const zoom = d3.zoom().scaleExtent([0.08, 3]).on("zoom", (e) => viewport.attr("transform", e.transform));
svg.call(zoom).on("dblclick.zoom", null);

const tooltip = $("tooltip");

function showTooltip(event, d) {
  const data = d.data;
  const pos = data.line ? `рядок ${data.line}${data.col ? `, стовпець ${data.col}` : ""}` : "";
  tooltip.innerHTML = `<b>${escapeHtml(data.kind)}</b> <code>${escapeHtml(data.label || "")}</code>` +
    (data.type ? `<div>тип: <code>${escapeHtml(data.type)}</code></div>` : "") +
    (data.role ? `<div>роль: ${escapeHtml(data.role)}</div>` : "") +
    (pos ? `<div>${pos}</div>` : "") +
    (d.hiddenCount ? `<div>згорнуто вузлів: ${d.hiddenCount}</div>` : "");
  tooltip.hidden = false;
  moveTooltip(event);
}

function moveTooltip(event) {
  const box = $("tree").getBoundingClientRect();
  const w = tooltip.offsetWidth;
  const h = tooltip.offsetHeight;
  const x = Math.min(event.clientX - box.left + 14, box.width - w - 8);
  const y = Math.min(event.clientY - box.top + 14, box.height - h - 8);
  tooltip.style.left = `${Math.max(8, x)}px`;
  tooltip.style.top = `${Math.max(8, y)}px`;
}

function focusNode(d, event) {
  state.activeKey = d.data.key;
  nodeLayer.selectAll(".node").classed("active", (n) => n.data.key === state.activeKey);
  setMark(d.data);
  if (event && event.type === "focus") revealMark();
}

function clearFocus() {
  state.activeKey = null;
  nodeLayer.selectAll(".node").classed("active", false);
  tooltip.hidden = true;
  setMark(null);
}

function nodeSub(data) {
  return shorten(data.type || data.kind);
}

function renderTree() {
  if (!state.data) {
    linkLayer.selectAll("*").remove();
    roleLayer.selectAll("*").remove();
    nodeLayer.selectAll("*").remove();
    return;
  }
  const view = viewOf(state.data, state.showImplicit);
  assignKeys(view, "0");
  const root = d3.hierarchy(view, (d) => (state.collapsed.has(d.key) ? null : d.children));
  root.each((n) => {
    const labelW = textWidth(shorten(n.data.label || n.data.kind), "600 12px monospace");
    const subW = textWidth(nodeSub(n.data), "10px monospace");
    n.w = Math.max(labelW, subW) + 22;
    n.hiddenCount = state.collapsed.has(n.data.key) ? countNodes(n.data) - 1 : 0;
  });
  const horizontal = state.horizontal;
  const layout = d3.tree();
  if (horizontal) {
    let maxW = 0;
    root.each((n) => { maxW = Math.max(maxW, n.w); });
    layout.nodeSize([NODE_HEIGHT + 14, maxW + 56]).separation((a, b) => (a.parent === b.parent ? 1 : 1.2));
  } else {
    layout.nodeSize([1, NODE_HEIGHT + 44]).separation((a, b) => (a.w + b.w) / 2 + (a.parent === b.parent ? 14 : 26));
  }
  layout(root);
  const px = (n) => (horizontal ? n.y : n.x);
  const py = (n) => (horizontal ? n.x : n.y);
  const nodes = root.descendants();
  const links = root.links();
  const t = d3.transition().duration(DURATION);
  const linkPath = horizontal
    ? (l) => `M${px(l.source) + l.source.w / 2},${py(l.source)}C${(px(l.source) + px(l.target)) / 2},${py(l.source)} ${(px(l.source) + px(l.target)) / 2},${py(l.target)} ${px(l.target) - l.target.w / 2},${py(l.target)}`
    : (l) => `M${px(l.source)},${py(l.source) + NODE_HEIGHT / 2}C${px(l.source)},${(py(l.source) + py(l.target)) / 2} ${px(l.target)},${(py(l.source) + py(l.target)) / 2} ${px(l.target)},${py(l.target) - NODE_HEIGHT / 2}`;

  linkLayer.selectAll("path").data(links, (l) => l.target.data.key).join(
    (enter) => enter.append("path").attr("class", "link").attr("d", linkPath).attr("opacity", 0).call((s) => s.transition(t).attr("opacity", 1)),
    (update) => update.call((s) => s.transition(t).attr("d", linkPath)),
    (exit) => exit.remove(),
  );

  const labelled = links.filter((l) => l.target.data.role);
  const rolePos = (l) => (horizontal
    ? { x: px(l.target) - l.target.w / 2 - 4, y: py(l.target) - 8 }
    : { x: px(l.target) + 4, y: py(l.target) - NODE_HEIGHT / 2 - 5 });
  roleLayer.selectAll("text").data(labelled, (l) => l.target.data.key).join(
    (enter) => enter.append("text").attr("class", "role"),
    (update) => update,
    (exit) => exit.remove(),
  ).text((l) => l.target.data.role)
    .attr("text-anchor", horizontal ? "end" : "start")
    .attr("x", (l) => rolePos(l).x)
    .attr("y", (l) => rolePos(l).y);

  const groups = nodeLayer.selectAll("g.node").data(nodes, (n) => n.data.key).join(
    (enter) => {
      const g = enter.append("g")
        .attr("tabindex", 0)
        .attr("role", "button")
        .attr("transform", (n) => {
          const p = n.parent || n;
          return `translate(${px(p)},${py(p)})`;
        })
        .attr("opacity", 0);
      g.append("rect").attr("rx", 7).attr("ry", 7);
      g.append("text").attr("class", "label").attr("y", -2);
      g.append("text").attr("class", "sub").attr("y", 12);
      g.append("circle").attr("class", "badge").attr("r", 9);
      g.append("text").attr("class", "badge-text").attr("y", 3.5);
      return g;
    },
    (update) => update,
    (exit) => exit.remove(),
  );

  groups.attr("class", (n) => `node cat-${n.data.cat}`)
    .classed("active", (n) => n.data.key === state.activeKey)
    .attr("aria-label", (n) => `${n.data.kind} ${n.data.label || ""}${n.data.type ? `, тип ${n.data.type}` : ""}`);
  groups.select("rect")
    .attr("x", (n) => -n.w / 2)
    .attr("y", -NODE_HEIGHT / 2)
    .attr("width", (n) => n.w)
    .attr("height", NODE_HEIGHT);
  groups.select(".label").text((n) => shorten(n.data.label || n.data.kind));
  groups.select(".sub").text((n) => nodeSub(n.data));
  groups.select(".badge")
    .attr("display", (n) => (n.hiddenCount ? null : "none"))
    .attr("cx", (n) => (horizontal ? n.w / 2 : 0))
    .attr("cy", () => (horizontal ? 0 : NODE_HEIGHT / 2));
  groups.select(".badge-text")
    .attr("display", (n) => (n.hiddenCount ? null : "none"))
    .attr("x", (n) => (horizontal ? n.w / 2 : 0))
    .attr("y", () => (horizontal ? 3.5 : NODE_HEIGHT / 2 + 3.5))
    .text((n) => (n.hiddenCount > 99 ? "99+" : `${n.hiddenCount}`));

  groups.on("click", (event, n) => toggle(n))
    .on("keydown", (event, n) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        toggle(n);
      }
    })
    .on("mouseenter", (event, n) => { focusNode(n, event); showTooltip(event, n); })
    .on("mousemove", moveTooltip)
    .on("mouseleave", clearFocus)
    .on("focus", (event, n) => focusNode(n, event))
    .on("blur", clearFocus);

  groups.transition(t)
    .attr("opacity", 1)
    .attr("transform", (n) => `translate(${px(n)},${py(n)})`);

  state.bounds = {
    x0: d3.min(nodes, (n) => px(n) - n.w / 2) - 16,
    x1: d3.max(nodes, (n) => px(n) + n.w / 2) + 16,
    y0: d3.min(nodes, (n) => py(n)) - NODE_HEIGHT / 2 - 48,
    y1: d3.max(nodes, (n) => py(n)) + NODE_HEIGHT / 2 + 40,
  };
  if (state.needsFit) fit();
}

function toggle(n) {
  if (!n.data.children.length) return;
  if (state.collapsed.has(n.data.key)) state.collapsed.delete(n.data.key);
  else state.collapsed.add(n.data.key);
  renderTree();
}

function fit() {
  const box = svg.node().getBoundingClientRect();
  if (!state.bounds || !box.width || !box.height) {
    state.needsFit = true;
    return;
  }
  state.needsFit = false;
  const b = state.bounds;
  const k = Math.min(1.1, box.width / (b.x1 - b.x0), box.height / (b.y1 - b.y0));
  const scale = Math.max(k, 0.08);
  const tx = (box.width - (b.x1 - b.x0) * scale) / 2 - b.x0 * scale;
  const ty = Math.max(0, (box.height - (b.y1 - b.y0) * scale) / 2) - b.y0 * scale;
  const target = d3.zoomIdentity.translate(tx, ty).scale(scale);
  svg.transition().duration(DURATION).call(zoom.transform, target);
}

function exportSvg() {
  const b = state.bounds;
  if (!b) return;
  const clone = svg.node().cloneNode(true);
  const source = svg.node().querySelectorAll("*");
  const copies = clone.querySelectorAll("*");
  const props = ["fill", "stroke", "stroke-width", "stroke-dasharray", "font-family", "font-size", "font-weight", "text-anchor", "opacity", "paint-order"];
  source.forEach((el, i) => {
    const cs = getComputedStyle(el);
    props.forEach((p) => copies[i].style.setProperty(p, cs.getPropertyValue(p)));
  });
  clone.querySelector("g").removeAttribute("transform");
  clone.setAttribute("xmlns", "http://www.w3.org/2000/svg");
  clone.setAttribute("viewBox", `${b.x0} ${b.y0} ${b.x1 - b.x0} ${b.y1 - b.y0}`);
  clone.setAttribute("width", b.x1 - b.x0);
  clone.setAttribute("height", b.y1 - b.y0);
  clone.style.background = getComputedStyle(document.body).backgroundColor;
  download("ast.svg", new XMLSerializer().serializeToString(clone), "image/svg+xml");
}

function download(name, content, type) {
  const url = URL.createObjectURL(new Blob([content], { type }));
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  URL.revokeObjectURL(url);
}

function setStatus(text, bad) {
  const el = $("status");
  el.textContent = text;
  el.classList.toggle("bad", !!bad);
}

function renderDiagnostics() {
  const list = $("diagnostics");
  list.innerHTML = "";
  state.diagnostics.forEach((d) => {
    const li = document.createElement("li");
    li.className = d.severity;
    const b = document.createElement("button");
    const pos = document.createElement("span");
    pos.className = "pos";
    pos.textContent = `${d.line}:${d.col}`;
    const sev = document.createElement("span");
    sev.className = "sev";
    sev.textContent = d.severity === "error" ? "помилка" : "попередження";
    b.append(pos, sev, d.message);
    b.addEventListener("click", () => goTo(d.line, d.col));
    li.append(b);
    list.append(li);
  });
}

function goTo(line, col) {
  const lines = editor.value.split("\n");
  let offset = 0;
  for (let i = 0; i < line - 1 && i < lines.length; i++) offset += lines[i].length + 1;
  const target = line <= lines.length ? offset + byteColToIndex(lines[line - 1], col) : editor.value.length;
  editor.focus();
  editor.setSelectionRange(target, target);
  const top = (line - 1) * 20;
  if (top < editor.scrollTop || top > editor.scrollTop + editor.clientHeight - 40) editor.scrollTop = Math.max(0, top - editor.clientHeight / 2);
  syncScroll();
}

function renderAsm() {
  const el = $("asm");
  if (!state.asm) {
    el.textContent = "Асемблер з'явиться, коли код пройде семантичний аналіз.";
    $("asm-meta").textContent = "";
    return;
  }
  el.innerHTML = state.asm.split("\n").map((line) => {
    const safe = escapeHtml(line);
    if (/^\S.*:$/.test(line)) return `<span class="a-label">${safe}</span>`;
    if (/^\s*\./.test(line)) return `<span class="a-dir">${safe}</span>`;
    return safe.replace(/^(\s*)(\S+)/, '$1<span class="a-op">$2</span>');
  }).join("\n");
  $("asm-meta").textContent = `${state.asm.trimEnd().split("\n").length} рядків`;
}

function renderTokens(tokens) {
  const body = $("tokens");
  body.innerHTML = "";
  const frag = document.createDocumentFragment();
  tokens.forEach((t) => {
    const tr = document.createElement("tr");
    [`${t.line}:${t.col}`, t.kind, t.text].forEach((v) => {
      const td = document.createElement("td");
      td.textContent = v;
      tr.append(td);
    });
    frag.append(tr);
  });
  body.append(frag);
}

function renderRun(run, fatal) {
  const out = $("stdout");
  const meta = $("run-meta");
  out.classList.remove("fail");
  if (fatal) {
    out.textContent = fatal;
    out.classList.add("fail");
    meta.textContent = "";
    return;
  }
  if (!run) {
    out.textContent = "Програму не виконано: виправте помилки в коді.";
    out.classList.add("fail");
    meta.textContent = "";
    return;
  }
  out.textContent = run.stdout;
  if (run.failed) {
    const span = document.createElement("span");
    span.className = "err-line";
    span.textContent = `${run.stdout && !run.stdout.endsWith("\n") ? "\n" : ""}Помилка виконання${run.line ? ` у рядку ${run.line}` : ""}: ${run.error}`;
    out.append(span);
    out.classList.add("fail");
    meta.textContent = `кроків: ${run.steps.toLocaleString("uk-UA")}`;
  } else {
    meta.textContent = `код виходу ${run.exit} · кроків: ${run.steps.toLocaleString("uk-UA")}`;
    if (!run.stdout) out.textContent = "(програма нічого не вивела)";
  }
}

async function compile(execute) {
  const id = ++state.requestId;
  const runButton = $("run");
  if (execute) runButton.disabled = true;
  try {
    const response = await fetch("/api/compile", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source: editor.value, stdin: $("stdin").value, execute }),
    });
    const result = await response.json();
    if (id !== state.requestId) return;
    apply(result, execute);
  } catch (error) {
    if (id !== state.requestId) return;
    setStatus(`Немає зв'язку із сервером: ${error.message}`, true);
    if (execute) renderRun(null, `Немає зв'язку із сервером: ${error.message}`);
  } finally {
    if (execute) runButton.disabled = false;
  }
}

function apply(result, execute) {
  if (result.fatal) {
    setStatus(result.fatal, true);
    if (execute) renderRun(null, result.fatal);
    return;
  }
  state.diagnostics = result.diagnostics;
  renderDiagnostics();
  const errors = result.diagnostics.filter((d) => d.severity === "error").length;
  const warnings = result.diagnostics.length - errors;
  const hadTree = !!state.data;
  if (result.ast) {
    state.data = result.ast;
    $("stale").hidden = true;
  } else {
    $("stale").hidden = !hadTree;
  }
  state.asm = result.asm || "";
  renderAsm();
  renderTokens(result.tokens);
  renderHighlight();
  renderTree();
  const parts = [];
  if (result.ast) parts.push(`вузлів AST: ${countNodes(state.data)}`);
  if (errors) parts.push(`помилок: ${errors}`);
  if (warnings) parts.push(`попереджень: ${warnings}`);
  if (!errors) parts.push("код коректний");
  setStatus(parts.join(" · "), errors > 0);
  if (execute) renderRun(result.run, null);
}

let debounce = null;

function scheduleCompile() {
  clearTimeout(debounce);
  debounce = setTimeout(() => compile(false), 300);
}

function selectTab(name) {
  document.querySelectorAll(".tabs button").forEach((b) => b.setAttribute("aria-selected", String(b.dataset.tab === name)));
  document.querySelectorAll(".tab").forEach((t) => t.classList.toggle("active", t.id === `tab-${name}`));
  if (name === "ast" && state.needsFit) renderTree();
}

function collapseExpressions() {
  state.collapsed.clear();
  const view = viewOf(state.data, state.showImplicit);
  assignKeys(view, "0");
  const walk = (n) => {
    if (n.kind === "ExprStmt" || n.kind === "VarDecl" || n.kind === "Return") {
      if (n.children.length) state.collapsed.add(n.key);
      return;
    }
    n.children.forEach(walk);
  };
  walk(view);
  state.needsFit = true;
  renderTree();
}

function loadExample(example) {
  editor.value = example.source;
  editor.scrollTop = 0;
  state.collapsed.clear();
  state.needsFit = true;
  state.mark = null;
  renderHighlight();
  compile(false);
}

function bindEvents() {
  editor.addEventListener("input", () => {
    state.mark = null;
    renderHighlight();
    scheduleCompile();
  });
  editor.addEventListener("scroll", syncScroll);
  editor.addEventListener("keydown", (event) => {
    if (event.key === "Tab" && !event.shiftKey) {
      event.preventDefault();
      editor.setRangeText("    ", editor.selectionStart, editor.selectionEnd, "end");
      editor.dispatchEvent(new Event("input"));
    }
    if (event.key === "Enter" && (event.ctrlKey || event.metaKey)) {
      event.preventDefault();
      $("run").click();
    }
  });
  $("run").addEventListener("click", () => {
    selectTab("output");
    compile(true);
  });
  document.querySelectorAll(".tabs button").forEach((b) => b.addEventListener("click", () => selectTab(b.dataset.tab)));
  $("fit").addEventListener("click", fit);
  $("expand").addEventListener("click", () => {
    state.collapsed.clear();
    state.needsFit = true;
    renderTree();
  });
  $("collapse-expr").addEventListener("click", () => { if (state.data) collapseExpressions(); });
  $("implicit").addEventListener("change", (e) => {
    state.showImplicit = e.target.checked;
    state.collapsed.clear();
    state.needsFit = true;
    renderTree();
  });
  $("horizontal").addEventListener("change", (e) => {
    state.horizontal = e.target.checked;
    state.needsFit = true;
    renderTree();
  });
  $("export-svg").addEventListener("click", exportSvg);
  $("copy-asm").addEventListener("click", async () => {
    if (!state.asm) return;
    try {
      await navigator.clipboard.writeText(state.asm);
      $("asm-meta").textContent = "скопійовано";
    } catch {
      $("asm-meta").textContent = "не вдалося скопіювати";
    }
  });
  $("download-asm").addEventListener("click", () => { if (state.asm) download("program.s", state.asm, "text/plain"); });
  window.addEventListener("resize", () => { if (state.bounds) fit(); });
}

async function init() {
  bindEvents();
  renderAsm();
  try {
    const response = await fetch("/api/examples");
    const examples = await response.json();
    const select = $("examples");
    examples.forEach((ex, i) => {
      const opt = document.createElement("option");
      opt.value = String(i);
      opt.textContent = ex.name;
      select.append(opt);
    });
    select.addEventListener("change", () => loadExample(examples[Number(select.value)]));
    const start = Math.max(0, examples.findIndex((e) => e.name === "expression"));
    select.value = String(start);
    if (examples.length) loadExample(examples[start]);
  } catch (error) {
    setStatus(`Немає зв'язку із сервером: ${error.message}`, true);
  }
}

init();
