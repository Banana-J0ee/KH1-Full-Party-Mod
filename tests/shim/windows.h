/* Minimal Windows API shim so the install code can be run on Linux against a
 * copy of the game image (tests/harness.c). Only what the mod uses. */
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <strings.h>
typedef void *HANDLE, *HINSTANCE, *LPVOID, *PVOID;
typedef uint32_t DWORD; typedef int BOOL; typedef int32_t LONG; typedef uint16_t WORD; typedef uint8_t BYTE;
typedef uint64_t ULONG_PTR, DWORD64; typedef size_t SIZE_T;
#define WINAPI
#define __stdcall
#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define GENERIC_WRITE 0x40000000
#define GENERIC_READ 0x80000000
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define FILE_SHARE_DELETE 4
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_NORMAL 0x80
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define PAGE_READWRITE 4
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_GUARD 0x100
#define PAGE_NOACCESS 1
#define DLL_PROCESS_ATTACH 1
#define MAX_PATH 260
#define EXCEPTION_ACCESS_VIOLATION 0xC0000005u
#define EXCEPTION_ILLEGAL_INSTRUCTION 0xC000001Du
#define EXCEPTION_INT_DIVIDE_BY_ZERO 0xC0000094u
#define EXCEPTION_STACK_OVERFLOW 0xC00000FDu
#define EXCEPTION_PRIV_INSTRUCTION 0xC0000096u
#define EXCEPTION_CONTINUE_SEARCH 0
typedef struct { int dummy; } CRITICAL_SECTION;
static inline void InitializeCriticalSection(CRITICAL_SECTION *c) { (void)c; }
static inline void EnterCriticalSection(CRITICAL_SECTION *c) { (void)c; }
static inline void LeaveCriticalSection(CRITICAL_SECTION *c) { (void)c; }
static inline HANDLE CreateFileA(const char *p, DWORD a, DWORD b, void *c, DWORD d, DWORD e, void *f)
{ (void)p;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f; return (HANDLE)1; }
static inline BOOL WriteFile(HANDLE h, const void *b, DWORD n, DWORD *w, void *o)
{ (void)h;(void)o; fwrite(b, 1, n, stdout); if (w) *w = n; return 1; }
static inline DWORD GetTickCount(void) { return 0; }
static inline void *VirtualAlloc(void *a, SIZE_T n, DWORD t, DWORD p)
{ (void)t;(void)p; void *r = mmap(a, n, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS|(a?MAP_FIXED_NOREPLACE:0), -1, 0);
  if (r == MAP_FAILED || (a && r != a)) { if (r != MAP_FAILED) munmap(r, n); return NULL; } return r; }
static inline BOOL VirtualProtect(void *a, SIZE_T n, DWORD p, DWORD *o) { (void)a;(void)n;(void)p; if (o) *o = 0x40; return 1; }
static inline HANDLE GetCurrentProcess(void) { return 0; }
static inline BOOL FlushInstructionCache(HANDLE h, const void *a, SIZE_T n) { (void)h;(void)a;(void)n; return 1; }
typedef struct { WORD e_magic; BYTE pad[0x3a]; LONG e_lfanew; } IMAGE_DOS_HEADER;
typedef struct { WORD Machine, NumberOfSections; DWORD a, b, c; WORD SizeOfOptionalHeader, Characteristics; } IMAGE_FILE_HEADER;
typedef struct { BYTE pad[56]; DWORD SizeOfImage; BYTE rest[240 - 60]; } IMAGE_OPTIONAL_HEADER64;
typedef struct { DWORD Signature; IMAGE_FILE_HEADER FileHeader; IMAGE_OPTIONAL_HEADER64 OptionalHeader; } IMAGE_NT_HEADERS64;
typedef struct { BYTE Name[8]; union { DWORD VirtualSize; } Misc; DWORD VirtualAddress, SizeOfRawData, PointerToRawData, a, b; WORD c, d; DWORD Characteristics; } IMAGE_SECTION_HEADER;
#define IMAGE_FIRST_SECTION(nt) ((IMAGE_SECTION_HEADER *)((BYTE *)&(nt)->OptionalHeader + (nt)->FileHeader.SizeOfOptionalHeader))
