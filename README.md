## About

This is a modified copy of Kent Recursive Calculator distributed at http://krc-lang.org .

## ChangeLog

### 2018.09.15

#### Ported to macOS
- replaced `sbrk` to `malloc` in order to compile successfully on macOS

#### Initial Commit
- [Here](https://github.com/homma/krc/tree/101fc43429fcf8d97a547ef8a08aceb0df1738c9).

## Source Code Formatting
````
$ clang-format -style="{BasedOnStyle: llvm, SortIncludes: false}" -i *.c *.h
````
## Plan

### Short Term
- make it looks like a plain C software
- remove BCPL dependency completely
- CAPITAL LETTER to small letter
- replace BOOL with bool and stdbool.h
- rename listpack to listlib
- separate main() from listlib
- separate commands into their own file
- fix write function to properly output into files
- fix command line options
- remove old comments
- add comments

### Long Term
- add where clause
- add record type
- add anonymous function
- add floating point numeral
- port to JavaScript

### Someday..
- add more I/O
- add FFI
- add n+k pattern
- add type system

### Done
- port to macOS
- apply formatter to all source files
- replace long long with int64_t with stdint.h
- rename bcpl to iolib

