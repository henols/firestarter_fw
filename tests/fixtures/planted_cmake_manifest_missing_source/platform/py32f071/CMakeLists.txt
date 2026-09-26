# Fixture input for tests/test_check_cmake_manifest.py — NOT the real
# platform/py32f071/CMakeLists.txt (which does not exist on this branch
# yet; Phase 124 lands it). Structurally imitates the real manifest read
# from /workspaces/firestarter_py32_ci/platform/py32f071/CMakeLists.txt.

set(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(PY32_SDK_ROOT "${FAKE_SDK_ROOT}")

set(FIRESTARTER_COMMON_SOURCES
    "${REPOSITORY_ROOT}/src/firestarter.cpp"
    "${REPOSITORY_ROOT}/src/proms/eeprom_28c.cpp"
    "${REPOSITORY_ROOT}/src/proms/flash_type_3.cpp"
)

set(PY32_PLATFORM_SOURCES
    src/main.cpp
)

set(PY32_SDK_SOURCES
    "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Src/py32f071_hal.c"
    "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Src/py32f071_hal_rcc.c"
)
