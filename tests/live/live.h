/* Host types, booleans, DOS error values of dos/dos.h and CHECK for the live
 * tests. A test with a 16 bit BOOL defines LIVE_OWN_BOOL and its BOOL first. */
#ifndef LIVE_H
#define LIVE_H

#include <stdint.h>
#include <stdio.h>

typedef uint32_t ULONG;
typedef int32_t LONG;
typedef uint16_t UWORD;
typedef int16_t WORD;
typedef uint8_t UBYTE;
typedef int8_t BYTE;
typedef intptr_t SIPTR;
#ifndef LIVE_OWN_BOOL
typedef int BOOL;
#endif

#define TRUE 1
#define FALSE 0
#define DOSTRUE (-1)
#define DOSFALSE 0

#define ERROR_NO_FREE_STORE 103
#define ERROR_BAD_NUMBER 115
#define ERROR_REQUIRED_ARG_MISSING 116
#define ERROR_OBJECT_IN_USE 202
#define ERROR_OBJECT_EXISTS 203
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_OBJECT_TOO_LARGE 207
#define ERROR_ACTION_NOT_KNOWN 209
#define ERROR_INVALID_COMPONENT_NAME 210
#define ERROR_OBJECT_WRONG_TYPE 212
#define ERROR_DISK_NOT_VALIDATED 213
#define ERROR_DISK_WRITE_PROTECTED 214
#define ERROR_DIRECTORY_NOT_EMPTY 216
#define ERROR_DEVICE_NOT_MOUNTED 218
#define ERROR_SEEK_ERROR 219
#define ERROR_DISK_FULL 221
#define ERROR_DELETE_PROTECTED 222
#define ERROR_WRITE_PROTECTED 223
#define ERROR_NOT_A_DOS_DISK 225
#define ERROR_NO_DISK 226
#define ERROR_NO_MORE_ENTRIES 232

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

#endif
