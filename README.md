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
$ clang-format -style="{BasedOnStyle: llvm, SortIncludes: false}" -i <FILE>
````
