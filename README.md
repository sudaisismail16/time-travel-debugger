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
