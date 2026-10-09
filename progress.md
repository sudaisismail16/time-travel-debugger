# time-travel-debugger
### Day 1: [30 september]
**Understanding the project**
- Read the project PDF and the `server.cpp` template.

**System setup**
- Installed WSL with Ubuntu on Windows..
- Installed `g++` and `git`.
- Connected VS Code to Ubuntu with the WSL extension.

**Github**
- Created the public GitHub repo, cloned it and pushed the `server.cpp`.
- Compiled the template with `g++ server.cpp -o server`.

### Day 2: [1 October] [1:00 AM]

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


### Day 3: [1 October] [1:00 AM] : Pass 0x0 (Validation)

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

### Day 4: [9 October] [1 PM] : Pass 0x1 
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

