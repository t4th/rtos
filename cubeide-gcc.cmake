# CMake toolchain definition for STM32CubeIDE
set(CMAKE_SYSTEM_NAME Generic)

set (CMAKE_SYSTEM_PROCESSOR "arm" CACHE STRING "")
set (CMAKE_SYSTEM_NAME "Generic" CACHE STRING "")

# Skip link step during toolchain validation.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Specify toolchain. NOTE When building from inside STM32CubeIDE the location of the toolchain is resolved by the "MCU Toolchain" project setting (via PATH).  
set(TOOLCHAIN_PREFIX   "arm-none-eabi-")
set(CMAKE_C_COMPILER   "${TOOLCHAIN_PATH}${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PATH}${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PATH}${TOOLCHAIN_PREFIX}g++.exe")
set(CMAKE_AR           "${TOOLCHAIN_PATH}${TOOLCHAIN_PREFIX}ar.exe")
set(CMAKE_LINKER       "${TOOLCHAIN_PATH}{TOOLCHAIN_PREFIX}ld.exe")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_PATH}${TOOLCHAIN_PREFIX}objcopy.exe")
set(CMAKE_RANLIB       "${TOOLCHAIN_PATH}${TOOLCHAIN_PREFIX}ranlib.exe")
set(CMAKE_SIZE         "${TOOLCHAIN_PATH}${TOOLCHAIN_PREFIX}size.exe")
set(CMAKE_STRIP        "${TOOLCHAIN_PATH}${TOOLCHAIN_PREFIX}ld.exe")

MESSAGE(STATUS "ss CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}")
MESSAGE(STATUS "ss CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
MESSAGE(STATUS "ss CMAKE_OBJCOPY: ${CMAKE_OBJCOPY}")
MESSAGE(STATUS "ss CMAKE_SIZE: ${CMAKE_SIZE}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)