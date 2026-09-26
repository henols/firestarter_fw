ENTRY(Reset_Handler)

/* Flash map. Geometry is NOT guessed here -- see platform/py32f071/CONFIG-STORAGE.md
 * §"Flash geometry": Puya PY32F07X Reference Manual V0.2 §4.1/§4.2.1/Table 4-1,
 * page = 256 B, sector = 8192 B, main flash 0x08000000..0x0801FFFF (128 KiB).
 * That record landed in a commit PRECEDING this file's first edit (CFG-02).
 */
MEMORY
{
    /* D-13 -- NAMED SEAM ONLY, ZERO LENGTH, for Phase 129 (PCB-03/FUT-N05) to cite.
     * See platform/py32f071/FLASH-PATH-AND-PCB.md §"Flash budget, as actually
     * reserved" and, in the Firestarter meta-repo, .planning/v1.23-FLASH-PATH-DECISION.md.
     *
     * READ THIS BEFORE GIVING IT A SIZE. Unlike the CONFIG region below -- which
     * sits at the TOP of flash and can grow downward without moving anything --
     * giving BOOTLOADER a non-zero length MOVES the application's ORIGIN. That is
     * a flash-map MIGRATION, not a resize: every previously flashed unit's vector
     * table address changes. The corrected cost: every already-flashed unit needs
     * a full re-flash over DFU or SWD instead of an in-place update -- precisely
     * the recovery paths a self-flash bootloader exists to avoid.
     *
     * (Correction, Phase 129 / 129-RESEARCH.md C-1: this comment previously
     * attributed the cost to the absence of a vector-table offset register; that
     * attribution was wrong. This part's CMSIS device header declares
     * __VTOR_PRESENT, and the compiled SystemInit writes SCB->VTOR at every
     * boot, so the vector table moving is the cheap half -- the one-time fleet
     * re-flash above is the expensive half.)
     *
     * Phase 129 must record the bootloader budget as an INTENT WITH THAT COST
     * ATTACHED, never as a number that looks already paid for.
     */
    BOOTLOADER (rx) : ORIGIN = 0x08000000, LENGTH = 0

    /* Shrunk from 128K so .text/.rodata physically CANNOT reach CONFIG (D-10).
     * 0x1E000 = 120K -> app occupies 0x08000000..0x0801DFFF (sectors 0..14). */
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 120K

    /* Sector 15 (RM Table 4-1: pages 480-511) -- D-18 amends D-10's shrink
     * quantum to one whole 8 KiB sector. The two slots below are different
     * PAGE erase units, which is what CFG-06 requires; aligning the region to
     * a whole sector additionally means no sector-granular erase of the app
     * region can ever clip it. 7680 B of this 8192 B reservation is deliberate
     * slack, reclaimable later by FUT-N05 or by additional config slots
     * without moving any address. */
    CONFIG (r)  : ORIGIN = 0x0801E000, LENGTH = 8K

    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 16K
}

/* Consumed by platform/py32f071/src/config_storage_flash.cpp (Plan 126-08). Two
 * slots, one page each, in DIFFERENT page erase units (CFG-06) -- so erasing
 * one cannot disturb the other, which is the whole atomicity property. */
PROVIDE(__config_page_size    = 256);
PROVIDE(__config_slot_a_start = ORIGIN(CONFIG));                   /* 0x0801E000, page 480 */
PROVIDE(__config_slot_b_start = ORIGIN(CONFIG) + 256);             /* 0x0801E100, page 481 -- a different page erase unit */
PROVIDE(__config_region_end   = ORIGIN(CONFIG) + LENGTH(CONFIG));  /* 0x08020000 */

/* Structural guards -- a violation here is a LINK failure, not a runtime
 * surprise. Deliberately only these two: no ASSERT below uses the modulo
 * operator on a region origin (that construct is rated the likeliest to need
 * adjustment on real arm-none-eabi-ld, and no ARM linker exists in this
 * environment to try it against). The sector-alignment and inside-the-
 * physical-part arithmetic lives instead in tests/test_py32_flash_map.py, a
 * venue that can actually run it. */
ASSERT(ORIGIN(FLASH) + LENGTH(FLASH) <= ORIGIN(CONFIG),
       "app region overlaps the reserved config region")
ASSERT(__config_slot_b_start - __config_slot_a_start == __config_page_size,
       "config slots must be exactly one page apart (different erase units)")

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size = 0x000;
_Min_Stack_Size = 0x400;

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > RAM

    ._user_heap_stack (NOLOAD) :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } > RAM

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
