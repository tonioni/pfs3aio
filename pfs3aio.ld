/* A filesystem must remain one CODE hunk for old RDB loaders. In particular,
 * libnix's ctype table pointer must not become a separate four-byte DATA hunk.
 * Relocations are still emitted normally; custom.S handles broken loaders
 * which cannot apply them.
 */
OUTPUT_FORMAT("amiga")
OUTPUT_ARCH(m68k)

/* Freestanding code has no C startup to initialize a global ExecBase pointer.
 * libnix's memcpy expects this symbol to name the location holding ExecBase.
 */
PROVIDE(_SysBase = 4);

SECTIONS
{
  .text 0 :
  {
    *(.text .text.*)
    *(.rodata .rodata.*)
    *(.data .data.*)
    *(.bss .bss.*)
    *(COMMON)
  }
}
