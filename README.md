# Time-Travel Debugger (TTDB) - Phase 01 (Server)

The server takes a program written in the custom language C--, runs it, and records the program state after every line. The recording is saved as `session.tdbg`, which the Phase 02 client will use to move backward and forward through the execution.

## Build and run

Linux, Mac, WSL:

```
g++ -Wall -o server main.cpp server.cpp
```

Windows (MinGW g++, PowerShell):

```
g++ -Wall -o server main.cpp server.cpp -lws2_32
```

Run:

```
./server            # listen on port 8080   (Windows: .\server)
./server 9000       # listen on another port
./server file       # no network: read source.bin from this folder and write session.tdbg
```

In network mode the server handles one client at a time and keeps running. In file mode, a failed stage prints the error, exits with code 1 and writes no `.tdbg` file.

## The C-- language

Each line is `keyword identifier params...` with at most 16 params.

| Instruction | Meaning |
|---|---|
| `func name p1 p2 ...` | start of a function with its parameters |
| `func_end` | end of the current function |
| `call name a1 a2 ...` | call a function with arguments |
| `set x v` | create or update variable `x` (`v` is a number or a variable) |
| `add / sub / mul / div x v` | result is stored in `x` |

Parameters are passed by reference: a change to a parameter inside the callee is visible to the caller after `func_end`.

```
func foo b
set a 10
add b a
func_end
func main
set k 10
call foo k
func_end
```

After this program runs, `k` is 20.

## Pipeline

**Stage 0 - Receive (`handleClient`)**
The client connects over TCP and sends the C-- program. The server streams it to `source.bin` in 64 KB chunks, then runs the four passes. A receive timeout of 5 seconds stops a client that stalls, and a declared size of 0 or more than 15 MB is rejected. Any error text printed by a pass is captured and sent back to the client instead of a `.tdbg` file.

**Pass 0x0 - Validation (`validateProgram`)**
Reads every line and keeps one flag, "inside a function". A `func` while inside, a `func_end` while outside, or a `func` left open at the end is an error. A flag is enough because nesting is not allowed, so no stack is needed.

**Pass 0x1 - Resolve (`resolveProgram`)**
Writes every line to `resolve.bin` as a record and keeps two lists: `FuncEntry` (function name and record position) and `PendingPatch` (position of each `call` record and the function it wants). When the file is complete, each call record is patched with the target function's position. Returns the position of `main`. Errors: no `main`, a call to an undefined function, or a duplicate function.

**Pass 0x2 - Execution (`executeProgram`)**
Starts at `main`, reads one record at a time, tokenizes it and executes it. A call stack of `Frame`s holds the live state: `call` pushes a frame, copies the argument values in and jumps to the function. `func_end` copies the parameter values back into the caller's variables, pops the frame and returns. After every line, `buildSnapshot` copies the call stack and `Timeline::record` appends it. The timeline is a doubly linked list. Errors: division by zero, undefined variable, wrong argument count, stack overflow, unknown instruction.

**Pass 0x3 - Serialization (`writeTdbg`)**
Writes the timeline to `session.tdbg` (layout below).

## Network protocol

All integers are little-endian.

```
Client -> server    length (8B) | source bytes
Server -> client    status (1B) | length (8B) | payload
```

The code builds on Windows (Winsock) and on Linux/Mac (POSIX sockets).

Status 0 means success and the payload is the complete `session.tdbg`. Status 1 means failure and the payload is the error message as text. The old `session.tdbg` is deleted before each request, so a failed run never returns a stale file.

## resolve.bin format

Records are stored one after another:

```
[offset 8B][size 4B][text]
```

The next record starts at `offset + 8 + 4 + size`, and the first is at 0. For a normal line the offset field holds the record's own position. For a `call` record it holds the position of the target function's `func` record, written during patching. The text of the line is not changed, so no size or position shifts.

## session.tdbg format

```
Header (20 bytes)      'TTDB' | version (4B) | stepCount (4B) | indexOffset (8B)
Snapshot stream        for each step: stackDepth (4B) then stackDepth Frame structs
Dense index            stepCount x 8B, the file position of each snapshot
```

The header is written field by field, not as one struct, so struct padding does not add bytes. It is written first as a placeholder and rewritten at the end, once `stepCount` and `indexOffset` are known.

To read step `n`: read `indexOffset` from the header, read the 8 bytes at `indexOffset + n*8` to get the snapshot position, seek there, read `stackDepth`, then read that many `Frame`s.

## Limits

| Constant | Value |
|---|---|
| `MAX_VARS_PER_FRAME` | 16 |
| `MAX_STACK_DEPTH` | 64 |
| `MAX_FUNCS` | 128 |
| Name length (variables and functions) | 31 characters |

## Design notes

- `Variable` and `Frame` use `char[32]` for names, not `string`, because a `string` cannot be written to a file with `fwrite`.
- Only the first `stackDepth` frames of each snapshot are written, which keeps the file small. The dense index makes the variable record size safe.
- `Stack<T>` is a linked-list stack and `Timeline` is a doubly linked list, both implemented without the STL containers.

## Tested cases

- The demo program: 6 snapshots, `k` becomes 20 in `main`.
- Nested calls (`main` -> `b` -> `c`) with a parameter changed in each.
- Rejected with an error and no `.tdbg`: missing `main`, call to an undefined function, nested `func`, missing `func_end`, division by zero, wrong argument count.
- Over a real socket: the demo (client file identical to the server's `session.tdbg`), each error case returned as text, an empty program, an oversized declared length, a client that stalls (timeout after 5 s) and a client that disconnects mid-send. The server keeps serving afterwards.