# Byte

Byte is a small bytecode interpreter written in C: a single-pass compiler that
turns source into bytecode chunks, and a stack VM that executes them on an
expression stack.

```text
# tests/print.byte
print 1 + 2
# 3
```

Statements can be terminated by a newline or a `;`. See
[docs/language.md](docs/language.md) for the full grammar.

## Quickstart

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # configure
cmake --build build                            # build

./build/byte tests/print.byte                  # run a program file
./build/byte                                   # start the REPL
```

## Web playground

The interpreter also compiles to WebAssembly, behind an [Astro](https://astro.build)
site in [`web/`](web). `web/byte_web.c` replaces `main.c`: JavaScript passes a
source string to `byte_run()`, and Emscripten forwards `printf`/`fprintf` output
to the page.

Build the wasm module with the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html),
then start Astro:

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install latest
~/emsdk/emsdk activate latest
source ~/emsdk/emsdk_env.sh

./web/build.sh              # -> web/public/wasm/byte.mjs + byte.wasm
cd web
npm install
npm run dev                 # http://localhost:4321
```

The dev server serves the landing page at `/` and the playground at
`/playground/`.

If Astro leaves the dev server running in the background, stop it with
`npm run dev:stop`.

For a static build:

```bash
cd web
npm run build               # -> web/dist/
npm run preview             # serve the build locally
```

`npm run build:wasm` is a shortcut for `./web/build.sh`. Astro/Vite bundles the
client script and the worker; the Emscripten module is loaded at runtime from
`public/wasm/` so Vite leaves it (and its `.wasm` sibling) untouched.

Press **Run** (or <kbd>Ctrl/⌘</kbd> + <kbd>Enter</kbd>). A run is killed after
five seconds so an accidental `while true {}` cannot lock up the tab; the
interpreter is restarted on the next run. The build in `web/dist/` is static,
but it must be served over HTTP — browsers block WebAssembly and ES modules on
`file://`.

| File | Purpose |
| --- | --- |
| `web/byte_web.c` | C entry point: reset VM, `byte_run(source)` |
| `web/build.sh` | Emscripten build into `web/public/wasm/` |
| `web/src/pages/index.astro` | Landing page |
| `web/src/pages/playground.astro` | Playground route and composition |
| `web/src/layouts/MarketingLayout.astro` | Landing and legal page shell |
| `web/src/components/marketing/` | Navigation, footer, icons |
| `web/src/components/` | Playground masthead and editor markup |
| `web/src/styles/marketing.css` | Landing theme and layout |
| `web/src/styles/global.css` | Playground theme, layout, bundled Geist type |
| `web/src/scripts/playground.js` | UI, run loop, 5s timeout |
| `web/src/scripts/landing.js` | Navigation, reveals, tagline, copy |
| `web/src/workers/interpreter.worker.js` | Loads the wasm module, relays output |
| `web/public/fonts/` | Geist Sans + Geist Mono (SIL OFL 1.1) |
| `web/public/og.png` | Social sharing card |

On the C side, `byte_run()` frees and re-initialises the VM before and after
every program, so a failed run leaks neither globals nor heap objects into the
next one. Return codes match the CLI: `0` success, `65` compile error, `70`
runtime error.

## Tests

Language tests are `.byte` programs in `tests/`, verified against snapshots in
`tests/golden/` (each snapshot pins a test's exit code, output and errors).

```bash
./tests/run.sh                 # run the suite
./tests/run.sh --update        # regenerate snapshots after an intentional change
ctest --test-dir build         # same suite through CMake/CTest
```

See the header of `tests/run.sh` for details.

## Documentation

| Document                                     | Contents                             |
| ---------------------------------------------| ------------------------------------- |
| [Language & grammar](docs/language.md)       | statement forms, termination rules, semantics |
| [Architecture](docs/architecture.md)         | scanner → compiler → VM pipeline, Pratt expressions |
| [Debugging](docs/debugging.md)               | disassembly and VM trace output      |
| [Pratt parsing](docs/pratt-parsing.md)       | notes on how expression parsing works |
| [Closures & upvalues](docs/closures.md)      | compile-time capture tables and runtime boxes |
