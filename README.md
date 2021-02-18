# joyflow

A flow-based programming environment

THIS IS STILL AN EARLY WORK-IN-PROGRESS PROJECT

PLAY AT YOUR OWN RISK AND PLAY WITH CAUTION


## Goals

* Easy to extend // defining new operator(s) should be easy
* Can be compiled to standalone execuable
  * Can generate command line interface automatically
* Can be compiled to dynamic-linked library
* Can be configed to include custom set of builtin operators
* Efficient
  * Can utilize all the CPU/Memory resources available
  * Can monitor resource usage and run under quota

## Progress

- [ 60%] An efficient table struct that enables 
         column-wise copy-on-write data sharing
         and fast manipulation on cell data
         and fast deletion of rows and columns
         and maybe concurrent access (?)
- [ 60%] DataCollection Implement
  - [DONE] Numeric cell
  - [DONE] POD data cell
  - [DONE] Blob data cell
  - [DONE] Variable length vector cell
  - [ 80%] Table merging
  - [TODO] Value iterpolation
  - [ 80%] Index mapping
  - [ 50%] Testing
  - [TODO] Parallel access
- [ 10%] Argument passing
- [ 10%] Value passing
- [ 60%] Task schedualer
  - [ 20%] Graph evaluation
    - [DONE] Loop Checking
    - [TODO] Interruption
    - [TODO] Memory Quota
    - [TODO] Progress Report
  - [TODO] Data reuse & optimization
- [TODO] More Data Types
  - [TODO] Geometry data
  - [TODO] Rasterized data
  - [TODO] Big numbers
  - [TODO] Datetime
- [TODO] Rendering
  - [TODO] 2D Canvas
  - [TODO] 3D Scene
- [ 50%] Statistics
- [ 50%] Profiler
- [TODO] Debugger
- [ 90%] Serialization
- [ 60%] Lua API
  - [ 80%] Read-only data interface
- [TODO] Builtin Operators
  - [ 90%] *Split*
  - [ 80%] **Join**
  - [ 10%] *Filter*
  - [TODO] *Map*
  - [TODO] *Reduce*
  - [ 50%] Loop
  - [TODO] For each
  - [DONE] Condition
  - [ 90%] Sort
  - [DONE] CSV I/O
  - [ 80%] Lua Script
  - [TODO] Cpp Script
  - [TODO] Cache?
- [TODO] Command line executor
- [TODO] DSL for graph creation
- [TODO] DSL for data processing


