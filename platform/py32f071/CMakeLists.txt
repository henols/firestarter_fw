cmake_minimum_required(VERSION 3.20)

set(
    CMAKE_TOOLCHAIN_FILE
    "${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake"
    CACHE FILEPATH ""
)

project(firestarter_py32f071 C CXX ASM)

include(FetchContent)

FetchContent_Declare(
    py32f071_sdk
    GIT_REPOSITORY https://github.com/OpenPuya/PY32F071_Firmware.git
    GIT_TAG 0ed2f4b4d3391eccfd4491006a30295fd78e32c2
    GIT_SHALLOW FALSE
)

FetchContent_GetProperties(py32f071_sdk)
if(NOT py32f071_sdk_POPULATED)
    FetchContent_Populate(py32f071_sdk)
endif()

# Host release assets must be named firestarter_<board>.hex using the
# RURP_BOARD_NAME reported by firmware.
set(TARGET_NAME firestarter_py32f071.elf)
set(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(PY32_SDK_ROOT "${py32f071_sdk_SOURCE_DIR}")
set(LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/linker/PY32F071xB_FLASH.ld")

# PY32_EXCLUDED: src/boards/uno_rurp_shield.cpp -- AVR board impl, no ARM analogue
# PY32_EXCLUDED: src/boards/leonardo_rurp_shield.cpp -- AVR board impl, no ARM analogue
# PY32_EXCLUDED: src/boards/rurp_common.cpp -- AVR-specific common
# PY32_EXCLUDED: src/dev_tools.cpp -- ARM uses the shared DEV_TOOLS=0 default
# PY32_EXCLUDED: src/boards/rurp_config_storage_eeprom.cpp -- AVR EEPROM backend, no ARM analogue
set(FIRESTARTER_COMMON_SOURCES
    "${REPOSITORY_ROOT}/src/firestarter.cpp"
    "${REPOSITORY_ROOT}/src/eprom_operations.cpp"
    "${REPOSITORY_ROOT}/src/hardware_operations.cpp"
    "${REPOSITORY_ROOT}/src/operation_utils.cpp"
    "${REPOSITORY_ROOT}/src/command_decoder.c"
    "${REPOSITORY_ROOT}/src/boards/rurp_serial_utils.cpp"
    "${REPOSITORY_ROOT}/src/rurp_vpp.cpp"
    "${REPOSITORY_ROOT}/src/rurp_config_utils.cpp"
    "${REPOSITORY_ROOT}/src/rurp_config_migrate.cpp"
    "${REPOSITORY_ROOT}/src/rurp_voltage_math.c"
    "${REPOSITORY_ROOT}/src/proms/memory.cpp"
    "${REPOSITORY_ROOT}/src/proms/eprom.cpp"
    "${REPOSITORY_ROOT}/src/proms/eprom_params.cpp"
    "${REPOSITORY_ROOT}/src/proms/eeprom_28c.cpp"
    "${REPOSITORY_ROOT}/src/proms/flash_nor_unlock.cpp"
    "${REPOSITORY_ROOT}/src/proms/flash_5v_page.cpp"
    "${REPOSITORY_ROOT}/src/proms/flash_intel.cpp"
    "${REPOSITORY_ROOT}/src/proms/flash_utils.cpp"
    "${REPOSITORY_ROOT}/src/proms/sram.cpp"
    "${REPOSITORY_ROOT}/src/proms/not_implemented.cpp"
)

set(PY32_PLATFORM_SOURCES
    startup/startup_py32f071.s
    src/main.cpp
    src/platform_compat.cpp
    src/timing.cpp
    src/usb_cdc.c
    src/py32f071_rurp_shield.cpp
    src/config_storage_flash.cpp
    src/config_storage_dualslot.cpp
)

set(PY32_SDK_SOURCES
    "${PY32_SDK_ROOT}/Templates/PY32F071xx_Templates/Src/system_py32f071.c"
    "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Src/py32f071_hal.c"
    "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Src/py32f071_hal_rcc.c"
    "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Src/py32f071_hal_rcc_ex.c"
    "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Src/py32f071_hal_gpio.c"
    "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Src/py32f071_hal_cortex.c"
    "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Src/py32f071_hal_pwr.c"
    "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Src/py32f071_hal_dma.c"
    "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Src/py32f071_hal_adc.c"
    "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Src/py32f071_hal_adc_ex.c"
    "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Src/py32f071_hal_tim.c"
    "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Src/py32f071_hal_flash.c"
    "${PY32_SDK_ROOT}/Middlewares/Third_Party/CherryUSB/core/usbd_core.c"
    "${PY32_SDK_ROOT}/Middlewares/Third_Party/CherryUSB/class/cdc/usbd_cdc.c"
    "${PY32_SDK_ROOT}/Middlewares/Third_Party/CherryUSB/port/usb_dc_py32.c"
)

add_executable(
    ${TARGET_NAME}
    ${FIRESTARTER_COMMON_SOURCES}
    ${PY32_PLATFORM_SOURCES}
    ${PY32_SDK_SOURCES}
)

target_include_directories(
    ${TARGET_NAME}
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/include"
        "${REPOSITORY_ROOT}/include"
        "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Inc"
        "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Inc/Legacy"
        "${PY32_SDK_ROOT}/Drivers/CMSIS/Device/PY32F071/Include"
        "${PY32_SDK_ROOT}/Drivers/CMSIS/Include"
        "${PY32_SDK_ROOT}/Middlewares/Third_Party/CherryUSB/port"
        "${PY32_SDK_ROOT}/Middlewares/Third_Party/CherryUSB/core"
        "${PY32_SDK_ROOT}/Middlewares/Third_Party/CherryUSB/common"
        "${PY32_SDK_ROOT}/Middlewares/Third_Party/CherryUSB/class/cdc"
)

# ARM uses the shared DEV_TOOLS=0 default.
# The pin-map guard requires an explicitly configured board to avoid silently
# compiling provisional GPIO mappings. No PY32 PCB exists to validate the DAC,
# so keep the VPP DAC capability disabled until real hardware is available.
target_compile_definitions(
    ${TARGET_NAME}
    PRIVATE
        USE_HAL_DRIVER
        PY32F071xB
        RURP_PLATFORM_PY32F071=1
        RURP_BOARD_NAME="py32f071"
        MONITOR_SPEED=250000
        DATA_BUFFER_SIZE=512
        RURP_PY32F071_PINMAP_CONFIGURED=1
        RURP_HAS_VPP_DAC=0
)

target_compile_features(${TARGET_NAME} PRIVATE c_std_11 cxx_std_17)

target_compile_options(
    ${TARGET_NAME}
    PRIVATE
        -mcpu=cortex-m0plus
        -mthumb
        -ffunction-sections
        -fdata-sections
        -fno-common
        -Wall
        -Wextra
        -Os
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

target_link_options(
    ${TARGET_NAME}
    PRIVATE
        -mcpu=cortex-m0plus
        -mthumb
        "-T${LINKER_SCRIPT}"
        -nostartfiles
        --specs=nano.specs
        --specs=nosys.specs
        -Wl,--gc-sections
        "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/firestarter_py32f071.map,--cref"
)

target_link_libraries(${TARGET_NAME} PRIVATE m)

set_target_properties(
    ${TARGET_NAME}
    PROPERTIES
        LINK_DEPENDS "${LINKER_SCRIPT}"
)

set(BIN_FILE "${CMAKE_CURRENT_BINARY_DIR}/firestarter_py32f071.bin")
set(HEX_FILE "${CMAKE_CURRENT_BINARY_DIR}/firestarter_py32f071.hex")

add_custom_command(
    TARGET ${TARGET_NAME}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}> "${BIN_FILE}"
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${TARGET_NAME}> "${HEX_FILE}"
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
    BYPRODUCTS
        "${BIN_FILE}"
        "${HEX_FILE}"
    VERBATIM
)
