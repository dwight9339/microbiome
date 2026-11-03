/* Embeds the compiled pack in flash. The engine reads it in place through the
 * XIP window, so it costs no SRAM. MB_PACK_PATH is set by CMakeLists.txt. */
    .section .rodata.mb_pack, "a"
    .balign 4
    .global mb_pack_start
mb_pack_start:
    .incbin MB_PACK_PATH
    .global mb_pack_end
mb_pack_end:
    .balign 4
