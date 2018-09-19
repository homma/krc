// common.h: common definitions

#ifndef COMMON_H

#include <stdint.h>   // for int64_t
#include <inttypes.h> // for PRId64
#include <limits.h>   // for __WORDSIZE

#if __WORDSIZE == 64

// type for machine words, used for all integer variables.
// 64-bit value.
typedef int64_t WORD;

// printf/scanf format to use with WORDs
#define W PRId64

#else

// 32-bit value.
typedef int WORD;
#define W "d"

#endif

// Trap broken compiler versions here, as it is included by everything
// Definitely doesn't work with gcc-4.9.[012]
#if __GNUC__ == 4 && __GNUC_MINOR__ == 9
// && __GNUC_PATCHLEVEL__  < 3
# error "KRC is broken when compiled with GCC 4.9. Earlier GCCs, clang and TinyC work.".
#endif

//// bool replaced from WORD to bool

// bool
// typedef WORD BOOL;
// #define FALSE 0
// #define TRUE 1

#include <stdbool.h>
typedef bool BOOL;
#define TRUE true
#define FALSE false

#define COMMON_H
#endif
