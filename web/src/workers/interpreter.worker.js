// Worker-side bridge to the Byte WebAssembly interpreter.
//
// The C interpreter is synchronous and can loop forever, so it runs off the
// main thread where the UI can terminate it after a timeout. Each program's
// output is forwarded line by line, letting the page stream results instead of
// waiting for the run to finish.

// The interpreter is built into public/wasm/ by web/build.sh. It is loaded at
// runtime rather than imported so Vite leaves the Emscripten output untouched.
const moduleUrl = new URL(`${import.meta.env.BASE_URL}wasm/byte.mjs`, self.location.origin).href;

let Module = null;
let currentId = null;
let fatal = false;
const pending = [];

function emit(kind, text) {
  if (currentId === null) return;
  self.postMessage({ type: "out", id: currentId, kind, text });
}

const booted = import(/* @vite-ignore */ moduleUrl)
  .then((namespace) =>
    namespace.default({
      print: (text) => emit("out", text),
      printErr: (text) => emit("err", text),
    }),
  )
  .then((module) => {
    Module = module;
    Module._byte_init();
    for (const message of pending.splice(0)) runProgram(message);
  })
  .catch((error) => {
    fatal = true;
    const detail = (error && error.message) || String(error);
    self.postMessage({
      type: "fatal",
      text:
        "Could not load the WebAssembly module: " +
        detail +
        " — run ./web/build.sh (or npm run build:wasm) to build public/wasm/.",
    });
  });

self.onmessage = (event) => {
  const message = event.data;
  if (!message || message.type !== "run" || fatal) return;

  if (Module) runProgram(message);
  else pending.push(message);
};

function runProgram({ id, source }) {
  currentId = id;

  let code = -1;
  try {
    const length = Module.lengthBytesUTF8(source) + 1;
    const pointer = Module._malloc(length);

    if (!pointer) throw new Error("Out of memory.");

    try {
      Module.stringToUTF8(source, pointer, length);
      code = Module._byte_run(pointer);
    } finally {
      Module._free(pointer);
    }
  } catch (error) {
    // A trap or abort inside the module; report it and let the UI restart us.
    emit("err", String((error && error.message) || error));
  }

  currentId = null;
  self.postMessage({ type: "done", id, code });
}
