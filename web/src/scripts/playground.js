// Playground UI. The interpreter itself lives in worker.js, which loads the
// Emscripten module from ./dist/.

const TIMEOUT_MS = 5000;

const EXAMPLES = [
  {
    id: "hello",
    name: "Hello, world",
    source: `# Byte runs top to bottom.
let greeting = "hello" + ", " + "world"
print greeting
`,
  },
  {
    id: "numbers",
    name: "Numbers",
    source: `let a = 17
let b = 5

print a + b
print a - b
print a * b
print a / b
print a > b
`,
  },
  {
    id: "loop",
    name: "For loop",
    source: `for i = 0; i < 5; i += 1 {
  print i * i
}
`,
  },
  {
    id: "fib",
    name: "Recursion",
    source: `func fib(n) {
  if n < 2 { return n }
  return fib(n - 1) + fib(n - 2)
}

for i = 0; i < 10; i += 1 {
  print fib(i)
}
`,
  },
  {
    id: "closure",
    name: "Closures",
    source: `func counter() {
  let count = 0

  func increment() {
    count += 1
    return count
  }

  return increment
}

let next = counter()
print next()
print next()
print next()
`,
  },
  {
    id: "error",
    name: "Runtime error",
    source: `# Reading an undeclared variable stops the program.
# Errors are printed to stderr and shown in red.
print total
`,
  },
];

const editor = document.getElementById("editor");
const gutter = document.getElementById("gutter");
const outputEl = document.getElementById("output");
const runBtn = document.getElementById("run");
const clearBtn = document.getElementById("clear");
const examplesEl = document.getElementById("examples");
const statusEl = document.getElementById("status");

let worker = null;
let running = false;
let activeId = 0;
let nextId = 1;
let startedAt = 0;
let timer = null;
let gutterLines = -1;

/* ---------------------------------------------------------------- output -- */

function clearOutput() {
  outputEl.replaceChildren();
}

function showEmpty() {
  const placeholder = document.createElement("div");
  placeholder.className = "empty";
  placeholder.innerHTML =
    '<span class="empty-glyph" aria-hidden="true">▶</span>' +
    "<p>Run your program to see output here</p>";
  outputEl.appendChild(placeholder);
}

function append(kind, text) {
  outputEl.querySelector(".empty")?.remove();

  const line = document.createElement("div");
  line.className = kind === "err" ? "line err" : "line";
  line.textContent = text == null ? "" : String(text);
  outputEl.appendChild(line);
  outputEl.scrollTop = outputEl.scrollHeight;
}

function setStatus(text, cls) {
  statusEl.textContent = text;
  statusEl.className = cls ? `status ${cls}` : "status";
}

/* ----------------------------------------------------------------- gutter -- */

function updateGutter() {
  const count = editor.value.split("\n").length;
  if (count === gutterLines) return;
  gutterLines = count;

  const fragment = document.createDocumentFragment();
  for (let number = 1; number <= count; number += 1) {
    const line = document.createElement("div");
    line.textContent = number;
    fragment.appendChild(line);
  }
  gutter.replaceChildren(fragment);
  gutter.scrollTop = editor.scrollTop;
}

function syncGutterScroll() {
  gutter.scrollTop = editor.scrollTop;
}

/* ----------------------------------------------------------------- worker -- */

// One long-lived worker: the C side resets its VM before every run, so state
// never leaks between programs. A run that overruns TIMEOUT_MS is killed by
// terminating the worker, and the next Run starts a fresh one.
function getWorker() {
  if (worker) return worker;

  worker = new Worker(new URL("../workers/interpreter.worker.js", import.meta.url), {
    type: "module",
  });
  worker.onmessage = onWorkerMessage;
  worker.onerror = onWorkerError;
  return worker;
}

function stopWorker() {
  if (!worker) return;
  worker.terminate();
  worker = null;
}

function onWorkerMessage(event) {
  const msg = event.data || {};

  if (msg.type === "fatal") {
    if (!running) return;
    append("err", msg.text);
    endRun("fatal");
    stopWorker();
    return;
  }

  if (!running || msg.id !== activeId) return;

  if (msg.type === "out") {
    append(msg.kind, msg.text);
  } else if (msg.type === "done") {
    endRun(msg.code);
  }
}

function onWorkerError(event) {
  event.preventDefault();
  if (!running) return;

  append(
    "err",
    "Failed to start the WebAssembly module. " +
      "Did you run ./web/build.sh? (See the browser console for details.)",
  );
  endRun("fatal");
  stopWorker();
}

/* -------------------------------------------------------------------- run -- */

function beginRun() {
  if (running) return;

  running = true;
  activeId = nextId++;
  startedAt = performance.now();
  runBtn.disabled = true;
  clearOutput();
  setStatus("running…", "running");

  const id = activeId;
  const source = editor.value;

  try {
    getWorker().postMessage({ type: "run", id, source });
  } catch (error) {
    append("err", String((error && error.message) || error));
    endRun("error");
    return;
  }

  timer = setTimeout(() => {
    if (!running) return;
    stopWorker();
    append(
      "err",
      `Stopped after ${TIMEOUT_MS / 1000}s — the program may be stuck in a ` +
        "loop. The interpreter was restarted.",
    );
    endRun("timeout");
  }, TIMEOUT_MS);
}

function endRun(code) {
  if (!running) return;

  running = false;
  runBtn.disabled = false;
  clearTimeout(timer);
  timer = null;

  const ms = Math.round(performance.now() - startedAt);

  if (code === 0) {
    if (!outputEl.querySelector(".line")) {
      const note = document.createElement("div");
      note.className = "line muted";
      note.textContent = "(no output)";
      outputEl.appendChild(note);
    }
    setStatus(`done · ${ms} ms`, "ok");
  } else if (code === 65) {
    setStatus(`compile error · ${ms} ms`, "bad");
  } else if (code === 70) {
    setStatus(`runtime error · ${ms} ms`, "bad");
  } else if (code === "timeout") {
    setStatus("stopped · timeout", "bad");
  } else if (code === "fatal") {
    setStatus("could not load wasm", "bad");
  } else {
    setStatus("run failed", "bad");
  }
}

/* ------------------------------------------------------------------- init -- */

for (const example of EXAMPLES) {
  const option = document.createElement("option");
  option.value = example.id;
  option.textContent = example.name;
  examplesEl.appendChild(option);
}

const defaultExample = EXAMPLES[0];
const requestedExample = new URLSearchParams(window.location.search).get("example");
const initialExample =
  EXAMPLES.find((example) => example.id === requestedExample) ?? defaultExample;

editor.value = initialExample.source;
examplesEl.value = initialExample.id;
updateGutter();
showEmpty();

examplesEl.addEventListener("change", () => {
  const example = EXAMPLES.find((item) => item.id === examplesEl.value);
  if (!example) return;
  editor.value = example.source;
  editor.scrollTop = 0;
  updateGutter();
  editor.focus();
});

runBtn.addEventListener("click", beginRun);

clearBtn.addEventListener("click", () => {
  clearOutput();
  showEmpty();
  setStatus("ready");
});

// Keep Tab inside the editor instead of moving focus.
editor.addEventListener("keydown", (event) => {
  if (event.key === "Tab" && !event.metaKey && !event.ctrlKey) {
    event.preventDefault();
    editor.setRangeText("  ", editor.selectionStart, editor.selectionEnd, "end");
    updateGutter();
  }
});

editor.addEventListener("input", updateGutter);
editor.addEventListener("scroll", syncGutterScroll, { passive: true });

// Cmd/Ctrl+Enter runs from anywhere on the page.
document.addEventListener("keydown", (event) => {
  if ((event.metaKey || event.ctrlKey) && event.key === "Enter") {
    event.preventDefault();
    beginRun();
  }
});

const isMac = /mac|iphone|ipad/i.test(
  navigator.userAgentData?.platform || navigator.platform || "",
);
const shortcut = isMac ? "⌘↵" : "Ctrl+↵";
document.getElementById("shortcut").textContent = shortcut;
document.getElementById("shortcut-hint").textContent = shortcut;
