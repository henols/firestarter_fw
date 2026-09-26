# Fixture input for tests/test_check_cmake_manifest.py — NOT the real
# platform/py32f071/CMakeLists.txt (which does not exist on this branch
# yet; Phase 124 lands it).

set(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(PY32_SDK_ROOT "${FAKE_SDK_ROOT}")

# PY32_EXCLUDED: src/boards/uno_rurp_shield.cpp
# The line above is deliberately malformed: it names a path but carries no
# "-- reason" segment. A PY32_EXCLUDED entry without a stated reason must
# itself be a gate violation.
set(FIRESTARTER_COMMON_SOURCES
    "${REPOSITORY_ROOT}/src/firestarter.cpp"
)

set(PY32_PLATFORM_SOURCES
    src/main.cpp
)

set(PY32_SDK_SOURCES
    "${PY32_SDK_ROOT}/Drivers/PY32F071_HAL_Driver/Src/py32f071_hal.c"
)
