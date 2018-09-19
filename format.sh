#!/bin/sh

clang-format -style="{BasedOnStyle: llvm, SortIncludes: false}" -i *.c *.h
