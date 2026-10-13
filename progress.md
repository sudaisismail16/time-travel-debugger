# time-travel-debugger
### Update 1: [30 september]
**Understanding the project**
- Read the project PDF and the `server.cpp` template.

**System setup**
- Installed WSL with Ubuntu on Windows..
- Installed `g++` and `git`.
- Connected VS Code to Ubuntu with the WSL extension.

**Github**
- Created the public GitHub repo, cloned it and pushed the `server.cpp`.
- Compiled the template with `g++ server.cpp -o server`.

### Update 2: [1 October] [1:00 AM]

**Data structures implemented**
- linked-list based call stack implemented with max depth check.
  - push
  - pop
  - peek
  - empty
  - snapshot_into
  - Timeline
  - record 
  - begin
  These functions are implemented

**Point understanded**
- In snapshot into, `callStack[0]` is the currently running function and `callStack[stackDepth - 1]` is the main.


### Update 3: [1 October] [1:00 AM] : Pass 0x0 (Validation)

**Functions implemented**
- `readSourceLine`: reads the next nonblank line and removes `\r` so files made on windows work. Returns `false` at the end of the file.
- `firstWord`: skips leading spaces and tabs and returns the first word.
- `secondWord`: reuses `firstWord`. It cuts off the first word and returns the first word of the rest line.
- `validateProgram`: scans the file once and checks the function structure.

**Design decision**
Nesting is not allowed so at most one function can be open at any time. A single `bool inside` flag is used `func` turns it on and `func_end` turns it off. A stack would only be needed if nested functions were allowed to match each `func_end` with the latest open `func` just like we did with the parenthesis function of brackets. The flag is simpler and uses constant memory.

- func while already inside a function | `Error: nested func at N` |
- func_end while no function is open | `Error: func_end without func at N` |
- End of file with a function still open | `Error: func without func_end` |


**Next:** Pass 0x1 which writes `resolve.bin` and patches `call` targets.
- took a whole day to understand this from 4-5 people in university.s

### Update 4: [9 October] [1 PM] : Pass 0x1 
**the idea**
- when call foo k instructions runs it needs to find where foo starts
- searching everytime would take alot of time so we store it somewhere
- so we create resolve.bin file where every line has an address
- except every call line has the address of the function it calls
- the format is [address of this record: 8 bytes][length of text: 4 bytes][text]
- so the next line starts at `address + 8 + 4 + length`

**the execution**
- `writeResolveRecord` writes one record. it reads the current position with `ftell` before writing, because that position is the records address.
- `readResolveRecord` reads one record back in the same order and returns the offset field or -1 if fails.

- `resolveProgram` does two jobs
1. Write every line as a record. also note down where each function starts in `funcArray` and where each `call` line is in `patches`.
2. After the whole file is written it goes back to each `call` record and overwrite its first 8 bytes with the address of the function it calls.


### Update 5: [12 October] [1 AM] : Pass 0x2 (Execution) Stage 3

**the idea**
- we execute the program line by line and take a snapshot of the call stack after every line
- a frame is one function call. it stores the function name, its parameters `argv` and its own variables `locals`
- the call stack holds the frames of all active function calls. the top frame is the function running right now
- the timeline is a doubly linked list of all the snapshots. phase 02 will move through it to go backward and forward

**helper functions**
- `findVar` finds a variable by name in one frame (parameters first, then locals)
- `isNumber` checks if the text is a number like 10 or -5
- `getValue` gives the value of an operand. it is either a number written in the code or a variable's value

**the functions**
- `tokenizeLine` splits a line into tokens. first word is the KEYWORD, second is the IDENTIFIER, the rest are PARAM. it returns how many tokens it found
- `buildSnapshot` makes a new `Snapshot` with `new` and fills it with `snapshot_into`. the depth of the stack is stored in `stackDepth`
- `executeProgram` is the main loop

**how executeProgram works**
1. make the `main` frame and push it on the stack. jump to main's address in resolve.bin and skip the `func main` header line
2. read one record, tokenize it and check the keyword
   - `set x 10` makes a local variable (or updates it if it already exists)
   - `add / sub / mul / div x y` update `x`. the result is saved in the first operand. division by zero stops with an error
   - `call foo k` makes a new frame for foo. the address of foo comes from the call record that pass 0x1 patched, so no searching. it remembers where to come back to (`returnLine`). it reads foo's own header line (`func foo b`) to learn the parameter names and copies the caller's values into them
   - `func_end` removes the top frame and goes back to the saved place
3. take a snapshot after every line and put it in the timeline
4. stop when the stack is empty because it means that main is finished

- in `foo`, `b` really means `k` from main. if foo changes `b`, `k` must change too
- when calling, the value of `k` is copied into `b`
- `argNames` remembers which caller variable each parameter came from
- on `func_end` the final value of `b` is copied back into `k`
- from outside it looks like `k` was changed by foo

**decisions made**
- only real instructions make a snapshot
- `func_end` of main also makes an empty snapshot with depth 0
- the first operand of add/sub/mul/div must already exist or `set` creates a new local if the name doesnt exist
- a call must have the same number of arguments as the function parameters
- on any error the execution stops and prints a message. the snapshots taken so far stays in the timeline
- the stack can only be 64 calls deep so more calls gives a stack overflow error


### Update 6: [13 October] [5 AM] : Pass 0x3 (Serialization) Stage 4

**the idea**
- after pass 0x2 the snapshots only exist in memory in the timeline. this pass saves them to `session.tdbg`
- the file has three parts header, snapshot stream, dense index
- the index has one entry for every snapshot. entry n is the address where snapshot n starts
- snapshots have different sizes so it reads the index entry and jumps there

**the functions**
- `writeHeader` the template only wrote the magic and the version, so i added `stepCount` and `indexOffset`. the header is 20 bytes: magic 4, version 4, stepCount 4, indexOffset 8. every field is written separately
- `writeVariable` writes the name with `writeString` and then the value
- `writeSnapshot` writes one snapshot field by field
- `writeTdbg` writes the whole file

**snapshot format**
- `stackDepth`
- for each frame from top to bottom:
  - function name
  - `argc` and then `argc` parameters
  - `returnLine`
  - `localCount` and then `localCount` name and value
- only the used parameters and locals are written

**writeTdbg**
- opens `session.tdbg` and write a placeholder header with `indexOffset = 0`. this only reserves the first 20 bytes
- walk through the timeline from `begin()` using `next`. before writing each snapshot, save the current position with `ftell` into the index array then write the snapshot
- after the last snapshot, the current position is `indexOffset`. write the index array there
- jump back to the start with `fseek` and write the real header
- the header is written twice because `indexOffset` is only known after all the snapshots are written

**decisions made**
- version is 1
- strings are stored as a 4 byte length followed by the characters
- index entries are 8 bytes each
- if the file can't be created an error is printed and nothing is written

