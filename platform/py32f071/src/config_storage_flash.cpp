/*
 * Project Name: Firestarter
 * Copyright (c) 2026 Henrik Olsson
 *
 * Permission is hereby granted under MIT license.
 */

/*
 * This is the ONLY
 * file in the whole tree that knows the PY32 HAL exists: every other TU that
 * touches configuration persistence -- the common policy layer
 * (src/rurp_config_utils.cpp) and the HAL-free dual-slot core
 * (config_storage_dualslot.{h,cpp}) -- goes through the two seam functions
 * defined at the bottom of this file and never sees a register or a HAL
 * type.
 *
 * This TU must never be compiled by the host test harness, which compiles
 * config_storage_dualslot.cpp directly against a RAM fake -- the
 * guard immediately below is what makes that true, matching the pattern in
 * platform/py32f071/src/py32f071_rurp_shield.cpp, never in the core.
 */

#if !defined(RURP_PLATFORM_PY32F071)
#error "config_storage_flash.cpp compiled for the wrong platform"
#endif

#include "boards/py32f071_rurp_shield.h"

#include <string.h>

#include "config_storage_dualslot.h"
#include "rurp_config_storage.h"

/*
 * Three linker symbols carry the WHOLE flash map -- declared here as
 * the only place this glue needs them, never duplicated as a literal
 * constant. platform/py32f071/linker/PY32F071xB_FLASH.ld is the single
 * source of truth; a rename on either side is an ARM link failure this
 * environment cannot detect (arm-none-eabi-gcc/cmake/ninja are all absent
 * here).
 */
extern "C"
{
    extern uint32_t __config_slot_a_start;
    extern uint32_t __config_slot_b_start;
    extern uint32_t __config_page_size;
}

namespace
{

uint32_t slot_addr(uint8_t slot)
{
    return slot == 0
               ? reinterpret_cast<uint32_t>(&__config_slot_a_start)
               : reinterpret_cast<uint32_t>(&__config_slot_b_start);
}

} // namespace

extern "C" bool hal_read(void *, uint8_t slot, void *dst, size_t len)
{
    // Flash is memory-mapped for reads on this part -- no HAL call needed,
    // just a plain copy from the mapped address.
    memcpy(dst, reinterpret_cast<const void *>(slot_addr(slot)), len);
    return true;
}

extern "C" bool hal_erase_page(void *, uint8_t slot)
{
    FLASH_EraseInitTypeDef erase = {};
    erase.TypeErase = FLASH_TYPEERASE_PAGEERASE; // the 256 B unit, RM V0.2 §4.2.3.3
    erase.PageAddress = slot_addr(slot);
    erase.NbPages = 1;
    uint32_t page_error = 0;

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return false;
    }

    // HAL_FLASH_Erase internally runs __HAL_FLASH_TIMMING_SEQUENCE_CONFIG()
    // (py32f071_hal_flash.c:416), which loads the FLASH_PERTPE / FLASH_SMERTPE
    // / FLASH_PRGTPE / FLASH_PRETPE timing registers from a factory table
    // keyed on the HSI output frequency. RM V0.2 §4.2.3.6 states plainly that
    // without those registers configured the operation "will fail" -- which
    // is exactly why this function never pokes FLASH->CR directly.
    const bool erase_ok = (HAL_FLASH_Erase(&erase, &page_error) == HAL_OK);

    HAL_FLASH_Lock();

    // RM §4.5.3 / §4.2.3.3: if the flash write-protection option byte covers
    // the config pages, the erase is SILENTLY SKIPPED by the hardware and
    // WRPERR is set instead of the page actually being erased. A non-HAL_OK
    // result here must propagate as failure and must never be reported as a
    // successful save (V4) -- the caller (rurp_dualslot_save) treats a false
    // return from erase_page as an immediate, unconditional save failure.
    return erase_ok;
}

extern "C" bool hal_program_page(void *, uint8_t slot, const uint32_t words[64])
{
    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return false;
    }

    // FLASH_Program_Page writes 64 32-bit words UNCONDITIONALLY from the
    // pointer it is given -- IS_FLASH_TYPEPROGRAM accepts exactly one value,
    // FLASH_TYPEPROGRAM_PAGE, and the HAL reads 64 words regardless of the
    // caller's object size. The core (config_storage_dualslot.cpp) always
    // hands over a full, 0xFF-pre-filled 64-word staging buffer -- never a
    // pointer to the smaller StoredConfiguration record -- so this call never
    // programs adjacent RAM into readable flash.
    //
    // This call is only correct on a page that was JUST erased.
    // RM §4.2.3.2 step 2 requires reading out a non-blank page's 64 words
    // before programming over it, and FLASH_Program_Page does not do that --
    // a call on a non-erased page is a defect, not a smaller or
    // silently-accepted overwrite.
    const bool program_ok =
        (HAL_FLASH_Program(FLASH_TYPEPROGRAM_PAGE, slot_addr(slot), const_cast<uint32_t *>(words)) == HAL_OK);

    HAL_FLASH_Lock();

    return program_ok;
}

namespace
{

const rurp_flash_primitives_t primitives = {
    hal_read,
    hal_erase_page,
    hal_program_page,
    nullptr,
};

} // namespace

/*
 * The two seam functions (include/rurp_config_storage.h) -- the py32
 * implementations of the platform-neutral persistence contract the common
 * policy layer (src/rurp_config_utils.cpp) calls without knowing which
 * backend answers. Both delegate directly to the HAL-free core over the
 * primitive table above.
 */
extern "C" bool rurp_config_storage_load(void *blob, size_t len)
{
    return rurp_dualslot_load(&primitives, blob, len);
}

extern "C" bool rurp_config_storage_save(const void *blob, size_t len)
{
    return rurp_dualslot_save(&primitives, blob, len);
}

/*
 * An already-satisfied prerequisite: RM §4.2.3 (p.35) requires the HSI to
 * be turned on for program and erase operations. platform/py32f071/src/main.cpp:25-27
 * already sets oscillator.HSIState = RCC_HSI_ON, HSIDiv = RCC_HSI_DIV1 and
 * HSICalibrationValue = RCC_HSICALIBRATION_24MHz, and makes HSI the PLL
 * source -- no change needed here. Recorded so a future clock refactor that
 * turns HSI off does not silently break config persistence: there would be
 * no compile or link error, only every save() and the write-back on a
 * virgin part failing on real silicon.
 */
