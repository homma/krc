## About

This is a modified copy of Kent Recursive Calculator distributed at http://krc-lang.org .

## ChangeLog

### 2018.09.25
- added list prcessing helper macros
  - `next`, `getcons`, `getatom`, `getint`, `kind`, `getval`, `getop`
  - to be used instead of `(HD(TL(HD(TL(HD(TL` ...

### 2018.09.22
- changed BCPL string to C string
- downcased `LIST`, `ATOM` and `TOKEN`
  - did not choose to use `List`, `Atom` or `Token`

### 2018.09.21
- replaced `WORD` with `word`
  - I did not choose to use `word_t` or `sword_t`
- removed unused code : `DECIMALS`, `READN`, `TERMINATOR`

### 2018.09.20
- applied formatter to all source files
- replaced `long long` with `int64_t` with stdint.h
- renamed bcpl to iolib
- replaced `BOOL` with `bool` and stdbool.h (experimental)
- removed emas.c emas.h
- removed emas prompt (we always use linenoise)
- renamed listpack to listlib
- renamed header files (`hdr` portion)

### 2018.09.19
- replaced BCPL related macros with their C equivalents

### 2018.09.15

- ported to macOS
  - replaced `sbrk` to `malloc` in order to compile successfully on macOS

#### Initial Commit
- [Here](https://github.com/homma/krc/tree/101fc43429fcf8d97a547ef8a08aceb0df1738c9).

## Source Code Formatting
````
$ clang-format -style="{BasedOnStyle: llvm, SortIncludes: false}" -i *.c *.h
````
## Plans

make it looks like a plain C software.  
keep it simple and small.

- remove BCPL dependency completely
- replace direct LISt Processings such as `HD(TL(HD(TL...` with proper names
- change CAPITAL LETTER to small letter
- remove global variables if unnecessary to be exposed
- separate main() from listlib
- separate commands into their own file
- fix write function to properly output into files
- fix command line options
- remove old comments
- add comments
- implement test cases
- change `(!( x == y ))` to `( x != y )` when there are only one condition
- replace linenoise with its unicode supported version
- reduce macros in order to apply source code formatter properly

## To be fixed
- `write "foo.txt" "foo"?` does not work
- `'#'?`, `'#'!` causes segmentation fault

## To be investigated
- what is INDIR?
  - it is used in primitive functions
- what is `+ 1` in leftprec, rightprec
  - how does it work?
- what is diprio stands for?
  - `di` priority

## Other known issues
- string cannot handle longer than 255 characters

