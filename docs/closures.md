# Closures and Upvalues

This note explains the closure implementation in Byte — why it is split between
the compiler and the VM, what each struct means, and how a captured variable
survives the death of the stack frame that declared it.

It corresponds to commits:

- `f4f1ab4` — wrap functions in closures (groundwork)
- `0a49460` — closures with upvalues
- `356d922` — closure tests

## The problem

A plain function is just code. A **closure** is code plus the variables it
captured from enclosing functions:

```js
func outer() {
  let x = "outside"
  func inner() {
    print x          // x is not a local of inner, nor a global
  }
  inner()
}
```

While `inner` runs, `x` still lives in `outer`'s stack frame, so a raw pointer
to that slot would work. But this must also work:

```js
func makeCounter() {
  let i = 0
  func count() {
    i = i + 1
    return i
  }
  return count       // the frame of makeCounter is about to disappear
}

let c = makeCounter()
print c()            // 1
print c()            // 2  -> same i, still alive
```

When `makeCounter` returns, `i`'s stack slot is reclaimed and will be reused by
later calls. The value must be moved somewhere that outlives the frame, and
every closure that captured `i` must keep seeing **the same** moved value.

Two constraints shape the design:

1. The compiler is single-pass and runs before any stack exists, so it cannot
   store runtime addresses.
2. At runtime a variable may outlive its frame, so the code cannot keep
   addressing a raw stack slot.

The split that resolves this:

- **Compile time** produces a static *descriptor table*: "this function
  captures N variables, here is where each one comes from."
- **Runtime** turns each descriptor into a heap-allocated **box**
  (`ObjUpvalue`) with an indirection pointer. When the frame dies the box is
  *closed* — the value is copied into the box and the pointer is retargeted at
  that copy. Code never changes; only the pointer moves.

## Two tables that must agree

| | Compile time | Runtime |
| --- | --- | --- |
| Where | `Compiler.upvalues[]` | `ObjClosure.upvalues[]` |
| Holds | descriptors `(index, isLocal)` | `ObjUpvalue*` boxes |
| Count | `ObjFunction.upvalueCount` | `ObjClosure.upvalueCount` |
| Lifetime | while the function is compiled | while the closure is alive |

`ObjFunction.upvalueCount` is the bridge: the compiler fills it while parsing,
`newClosure` uses it to size the runtime array, and `OP_CLOSURE` uses it to know
how many descriptors follow in the bytecode.

## Data structures

```c
// compiler.h
typedef struct {
  Token name;
  int depth;
  bool isCaptured;    // somebody in a nested function captured this
} Local;

typedef struct {
  uint8_t index;      // meaning depends on isLocal
  bool isLocal;
} Upvalue;

typedef struct Compiler {
  struct Compiler* enclosing;       // the function this one is nested in
  ObjFunction* function;
  FunctionType type;

  Local locals[UINT8_COUNT];
  int localCount;

  Upvalue upvalues[UINT8_COUNT];    // descriptors for THIS function
  int scopeDepth;
} Compiler;
```

```c
// object.h
typedef struct ObjUpvalue {
  Obj obj;
  Value* location;           // points at a stack slot while open, at &closed after
  Value closed;              // heap storage for the value after closing
  struct ObjUpvalue* next;   // sorted list of open upvalues in the VM
} ObjUpvalue;

typedef struct {
  Obj obj;
  ObjFunction* function;
  ObjUpvalue** upvalues;     // one box per captured variable
  int upvalueCount;
} ObjClosure;
```

```c
// vm.h
typedef struct {
  ...
  ObjUpvalue* openUpvalues;  // head of all currently open boxes
  ...
} VM;
```

## Compile time

The `Compiler` structs form a chain built by recursive descent:

```
Compiler(script) <- Compiler(outer) <- Compiler(middle) <- Compiler(inner)
                     enclosing           enclosing          enclosing
```

`initCompiler` links a new compiler to `current`; `endCompiler` restores it.
This chain is the compile-time mirror of the runtime call stack.

### 1. Marking a local as captured

`Local.isCaptured` starts `false` for every local (including the implicit slot 0
created in `initCompiler` and every local added by `addLocal`). It flips to
`true` only when a nested function resolves a name to that local:

```c
compiler->enclosing->locals[local].isCaptured = true;
```

That flag later tells `endScope` to emit `OP_CLOSE_UPVALUE` instead of
`OP_POP`.

### 2. Resolution order

Every identifier goes through `namedVariable`:

1. `resolveLocal(current, name)` → local slot → `OP_GET_LOCAL` / `OP_SET_LOCAL`
2. `resolveUpvalue(current, name)` → upvalue index → `OP_GET_UPVALUE` / `OP_SET_UPVALUE`
3. otherwise → global → `OP_GET_GLOBAL` / `OP_SET_GLOBAL`

### 3. `addUpvalue` interns the descriptor

```c
static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
  int upvalueCount = compiler->function->upvalueCount;

  for (int i = 0; i < upvalueCount; i++) {
    Upvalue* upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;                                   // already captured, reuse
    }
  }

  if (upvalueCount == UINT8_COUNT) {
    error("Too many closure variables in function.");
    return 0;
  }

  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}
```

The return value is the operand for `OP_GET_UPVALUE` / `OP_SET_UPVALUE`. It
must be stable, so identical captures are deduplicated by the pair
`(index, isLocal)` — three references to the same variable consume one slot, not
three.

`(index, isLocal)` is a safe identity because:

| `isLocal` | `index` means |
| --- | --- |
| `true` | stack slot in the **enclosing frame** |
| `false` | slot in the **enclosing closure's** `upvalues[]` array |

One slot identifies one variable, so the pair cannot collide.

### 4. `resolveUpvalue` walks outward, one hop at a time

```c
static int resolveUpvalue(Compiler* compiler, Token* name) {
  if (compiler->enclosing == NULL) return -1;

  int local = resolveLocal(compiler->enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(compiler, (uint8_t)local, true);   // hop 1: stack slot
  }

  int upvalue = resolveUpvalue(compiler->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(compiler, (uint8_t)upvalue, false); // hop 2: existing box
  }

  return -1;
}
```

The recursion is what makes grandparent captures work. With three levels:

```js
func outer() { let x = "x"; func middle() { func inner() { print x } } }
```

- `outer` owns `x` at slot 1.
- `middle` never mentions `x`, yet gets `upvalues[0] = {1, true}` — it captures
  from `outer`'s stack on behalf of its child.
- `inner` gets `upvalues[0] = {0, false}` — it reuses `middle`'s box.

Each descriptor describes exactly one level out. Long-range captures are chains
of one-level hops, never a single jump.

### 5. `namedVariable` must assign, not compare

```c
} else if ((arg = resolveUpvalue(current, &name)) != -1) {
```

This is the book's idiom, and the `=` is load-bearing: `arg` is the operand
emitted later. Writing `==` here compiles and runs, but silently turns every
non-local into `OP_GET_UPVALUE 255` — see [Failure modes](#failure-modes).

### 6. Emitting the descriptor stream

`function()` compiles the body, then emits the closure creation into the
**enclosing** chunk:

```c
ObjFunction* function = endCompiler();          // current is back to enclosing
emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

for (int i = 0; i < function->upvalueCount; i++) {
  emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
  emitByte(compiler.upvalues[i].index);
}
```

So the bytecode is:

```
OP_CLOSURE <constant index>  [isLocal, index] * upvalueCount
```

Note the asymmetry: the descriptors live in the **enclosing** function's chunk,
because that is where the closure is instantiated, but they describe the
**new** function's captures. `compiler.upvalues` (the local `Compiler` in
`function()`) still has the data after `endCompiler()` popped it.

`OP_CLOSURE` is therefore variadic in size: `1 + 2 * upvalueCount` bytes. Any
code that walks a chunk — especially the disassembler — must skip exactly that
many bytes.

### 7. Closing scope

```c
static void endScope() {
  current->scopeDepth--;
  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth > current->scopeDepth) {
    if (current->locals[current->localCount - 1].isCaptured) {
      emitByte(OP_CLOSE_UPVALUE);   // promote the box before dropping the slot
    } else {
      emitByte(OP_POP);
    }
    current->localCount--;
  }
}
```

`OP_POP` discards a value; `OP_CLOSE_UPVALUE` first copies it into its open box,
then discards the slot. Using `OP_POP` for a captured variable is the classic
dangling-pointer bug.

## Runtime

### The box is the whole idea

```
open:     closure->upvalues[0] --> ObjUpvalue { location --> vm.stack[3], closed = nil }
closed:   closure->upvalues[0] --> ObjUpvalue { location --> &closed,     closed = "outside" }
```

Closures never store values, only `ObjUpvalue*`. Two closures capturing the same
variable share one box, so after closing they still share one value. Closing
does not allocate a new box; it retargets `location` and copies the value into
`closed`. Every closure sees the move for free.

### `newClosure`

```c
ObjClosure* newClosure(ObjFunction* function) {
  ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) upvalues[i] = NULL;

  ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}
```

At this point every entry is `NULL`. The `OP_CLOSURE` handler must fill all of
them immediately; the VM indexes this array with bytecode operands and does not
null-check.

The script function is wrapped manually in `interpret()` with the same
constructor (`newClosure(function)`), even though it has zero upvalues.

### `captureUpvalue` interns open boxes

```c
static ObjUpvalue* captureUpvalue(Value* local) {
  ObjUpvalue* prevUpvalue = NULL;
  ObjUpvalue* upvalue = vm.openUpvalues;

  // list is sorted by descending location (highest stack address first)
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;                        // same slot already captured: share
  }

  ObjUpvalue* createdUpvalue = newUpvalue(local);
  createdUpvalue->next = upvalue;
  if (prevUpvalue == NULL) vm.openUpvalues = createdUpvalue;
  else prevUpvalue->next = createdUpvalue;

  return createdUpvalue;
}
```

Two jobs:

1. **Deduplicate.** If two closures capture the same stack slot, they get the
   same box. Without this they would each get their own box pointing at the same
   slot; while open they'd agree, but after closing each would hold a separate
   copy and assignments would stop being visible across closures.
2. **Keep a sorted list.** `vm.openUpvalues` is ordered from highest stack
   address to lowest, which makes `closeUpvalues` a simple head-pop.

`newUpvalue` sets `location`, `closed = NIL_VAL`, and `next = NULL`.

### `closeUpvalues`

```c
static void closeUpvalues(Value* last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue* upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;   // copy value to heap
    upvalue->location = &upvalue->closed;   // retarget every closure at once
    vm.openUpvalues = upvalue->next;
  }
}
```

`last` is the lowest stack address that is going away. Because the list is
sorted descending, everything that needs closing is at the head, so this is
O(number closed).

Two call sites:

- `OP_CLOSE_UPVALUE` → `closeUpvalues(vm.stackTop - 1)` then `pop()`
- `OP_RETURN` → `closeUpvalues(frame->slots)` before unwinding the frame

`resetStack` sets `vm.openUpvalues = NULL`; otherwise a second `interpret()`
would inherit dangling boxes from the previous run.

### Dispatch

```c
case OP_CLOSURE: {
  ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
  ObjClosure* closure = newClosure(function);
  push(OBJ_VAL(closure));

  for (int i = 0; i < closure->upvalueCount; i++) {
    uint8_t isLocal = READ_BYTE();
    uint8_t index = READ_BYTE();
    if (isLocal) {
      closure->upvalues[i] = captureUpvalue(frame->slots + index);
    } else {
      closure->upvalues[i] = frame->closure->upvalues[index];
    }
  }
  break;
}

case OP_GET_UPVALUE: {
  uint8_t slot = READ_BYTE();
  push(*frame->closure->upvalues[slot]->location);
  break;
}

case OP_SET_UPVALUE: {
  uint8_t slot = READ_BYTE();
  *frame->closure->upvalues[slot]->location = peek(0);
  break;
}

case OP_CLOSE_UPVALUE:
  closeUpvalues(vm.stackTop - 1);
  pop();
  break;
```

`isLocal == true` means the **current** frame's stack; `false` means the
**current closure's** array (the box was already made by this function's
enclosing instantiation).

## End to end: `tests/closure.byte`

```js
func outer() {
  let x = "outside"
  func inner() {
    print x
  }
  inner()
}
outer()
```

Disassembly (`-DBYTE_DEBUG_TRACE=ON`):

```
== inner ==
0000    4 OP_GET_UPVALUE      0
0002    5 OP_PRINT
0003    | OP_NIL
0004    | OP_RETURN

== outer ==
0000    2 OP_CONSTANT         0 'outside'
0002    5 OP_CLOSURE          1 <fn inner>
0004      |                     local 1
0006    6 OP_GET_LOCAL        2
0008    | OP_CALL             0
0010    7 OP_POP
0011    | OP_NIL
0012    | OP_RETURN

== <script> ==
0000    7 OP_CLOSURE          1 <fn outer>
0002    | OP_DEFINE_GLOBAL    0 'outer'
...
```

Reading it:

- Inside `inner`, `x` resolved to upvalue `0`. The operand is one byte.
- `outer`'s frame layout is `slot 0 = outer closure`, `slot 1 = x`,
  `slot 2 = inner`. The closure declaration emits `OP_CLOSURE` plus one
  descriptor pair: `local 1` — "capture stack slot 1 of the frame that is
  creating this closure."
- `OP_GET_LOCAL 2` fetches `inner` for the call. No upvalue is involved there.
- The disassembler prints the descriptor line because it reads
  `function->upvalueCount` from the constant and skips 2 bytes per upvalue.

Selected VM trace (each line is the stack state *after* the instruction):

```
before OP_CLOSURE inner:  [ <script> ][ <fn outer> ][ outside ]
after  OP_CLOSURE inner:  [ <script> ][ <fn outer> ][ outside ][ <fn inner> ]
after  OP_GET_LOCAL 2:    [ <script> ][ <fn outer> ][ outside ][ <fn inner> ][ <fn inner> ]
after  OP_GET_UPVALUE 0:  [ <script> ][ <fn outer> ][ outside ][ <fn inner> ][ <fn inner> ][ outside ]
```

`OP_GET_UPVALUE 0` dereferences the box, whose `location` is `&vm.stack[2]`
(absolute; `outer`'s `slots[1]`, holding `outside`), and pushes a copy.

## Escaping closures: `makeCounter`

```js
func makeCounter() {
  let i = 0
  func count() {
    i = i + 1
    return i
  }
  return count
}

let c = makeCounter()
print c()   // 1
print c()   // 2
```

Relevant bytecode:

```
== count ==
0000    4 OP_GET_UPVALUE      0
0002    | OP_CONSTANT         0 '1'
0004    | OP_ADD
0005    | OP_SET_UPVALUE      0
0007    5 OP_POP
0008    | OP_GET_UPVALUE      0
0010    6 OP_RETURN

== makeCounter ==
0000    2 OP_CONSTANT         0 '0'
0002    6 OP_CLOSURE          1 <fn count>
0004      |                     local 1
0006    7 OP_GET_LOCAL        2     ; load count
0008    8 OP_RETURN
```

There is no `OP_CLOSE_UPVALUE` here because `i` never leaves scope before the
function returns. `OP_RETURN` calls `closeUpvalues(frame->slots)`, which closes
**all** boxes pointing at or above the returning frame's base — including `i` at
slot 1. After that, `count`'s box owns the value on the heap, and both calls
share it, hence `1` then `2`.

If `closeUpvalues` were missing, `i` would still point into the dead frame; the
next call would reuse that slot, so `count()` would read/write some unrelated
temporary and print wrong values (or corrupt state) instead of counting.

## Invariants

- `function->upvalueCount` equals the number of descriptor pairs emitted after
  that function's `OP_CLOSURE`.
- `closure->upvalueCount == closure->function->upvalueCount`, and every
  `closure->upvalues[i]` is assigned by `OP_CLOSURE` before first use.
- Descriptor indices fit in one byte; `UINT8_COUNT` is the hard cap per
  function.
- Every captured local is marked `isCaptured`, and every captured local's scope
  ends with either `OP_CLOSE_UPVALUE` or an `OP_RETURN` that closes it.
- `vm.openUpvalues` is sorted by descending `location` and contains exactly the
  boxes whose `location` still points into the live stack.
- One stack slot maps to at most one open box (`captureUpvalue` dedupes).

## Failure modes

These are the ones that actually happened while building this, plus a few
waiting to happen.

**1. Emitting `OP_CLOSURE` without dispatching it.** The switch falls through
without consuming the constant/descriptor bytes, so the IP lands mid-instruction
and decodes garbage. The symptom was `Can only call functions.` pointing at the
wrong line, not an "unknown opcode" error. If disassembly looks right but
execution is nonsense, check that every opcode the compiler emits has a `case`.

**2. `arg == resolveUpvalue(...) != -1`.** Parses as
`arg == (resolveUpvalue(...) != -1)`, which is **always true** for `arg == -1`.
The branch is taken for locals-that-are-not-upvalues and for globals too, and
`arg` stays `-1`, so the emitted operand is `(uint8_t)-1 == 255`. The VM then
reads `closure->upvalues[255]` past the end of the allocation (zero-length for
the script closure, one entry for `inner`) and segfaults. Fix:

```c
} else if ((arg = resolveUpvalue(current, &name)) != -1) {
```

Note the side effect: even with the broken condition, `resolveUpvalue` still
ran and registered the upvalue, so the descriptor stream and disassembly looked
correct. Only the use-site operand was wrong.

**3. Forgetting `isCaptured = true`.** The local closes with `OP_POP`, the box
keeps pointing at a dead slot, and a later call observes/reuses that memory.

**4. No dedupe in `captureUpvalue`.** Two closures capturing the same slot get
two boxes. While the defining frame is alive both boxes point at the same slot,
so they appear to agree. When the frame closes, each box is copied separately
and the closures stop sharing:

```js
let inc = nil

func makeReader() {
  let i = 0
  func bump() { i = i + 1 }
  func read() { return i }
  inc = bump
  return read
}

let read = makeReader()
inc()
print read()   // 1 with dedupe, 0 if bump and read have separate boxes
```

**5. Not closing on return.** Same dangling-slot problem as (3), but at frame
granularity. `closeUpvalues(frame->slots)` in `OP_RETURN` is mandatory.

**6. Descriptor/`upvalueCount` mismatch.** The disassembler and the VM both trust
`upvalueCount`. If it disagrees with the emitted bytes, both desynchronize; the
disassembler prints nonsense and the VM reads operands from the middle of
instructions.

**7. Stale `openUpvalues` across runs.** `resetStack` must clear it, or boxes
from a previous `interpret()` remain reachable through a dangling list.

## Debugging

```bash
cmake -S . -B build-dbg -DBYTE_DEBUG_TRACE=ON
cmake --build build-dbg
./build-dbg/byte tests/closure.byte
```

See [debugging.md](debugging.md) for the trace format. The disassembler's
`OP_CLOSURE` case is the best way to confirm the descriptor stream:

```
0002    5 OP_CLOSURE          1 <fn inner>
0004      |                     local 1
```

Snapshot tests:

```bash
./tests/run.sh                    # run everything
./tests/run.sh --update closure   # regenerate tests/golden/closure.snap
```

## When the GC arrives

There is no mark phase yet; `freeObject` only frees. When GC is added:

- **Roots** must include the live stack (`vm.stack` up to `vm.stackTop`) and
  every `ObjUpvalue` in `vm.openUpvalues`. An open box may be referenced only by
  a closure that has not been marked yet, but the VM's list must keep it alive.
- **`ObjClosure`** must mark `function` and every non-NULL `upvalues[i]`.
- **`ObjUpvalue`** must mark `closed`, the value copied out of the stack; it may
  be the only reference to a string.
- **`ObjFunction`** must mark its constants, which are how nested
  `ObjFunction`s stay reachable.

## File map

| Concern | File |
| --- | --- |
| Parser/compiler `Local`, `Upvalue`, `Compiler` | `src/compiler/compiler.h` |
| Resolution, descriptor emission, scope closing | `src/compiler/compiler.c` |
| Opcodes | `src/core/chunk.h` |
| `ObjUpvalue`, `ObjClosure` | `src/core/object.h`, `src/core/object.c` |
| Capture, close, dispatch | `src/compiler/vm.c` |
| `openUpvalues` root | `src/compiler/vm.h` |
| Disassembly | `src/debug/debug.c` |
| Freeing | `src/utils/memory.c` |
| Test + snapshot | `tests/closure.byte`, `tests/golden/closure.snap` |

## Further reading

- Crafting Interpreters, chapter 25 "Closures" — the design this follows.
- [architecture.md](architecture.md) — where the compiler and VM sit in the
  pipeline.
- [debugging.md](debugging.md) — enabling disassembly and execution traces.
