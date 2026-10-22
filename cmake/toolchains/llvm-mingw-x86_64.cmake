# Cross-compiles 64-bit Windows binaries from Linux with llvm-mingw
# (https://github.com/mstorsjo/llvm-mingw), against Qt's "llvm_mingw" Windows build.
#
#   cmake -S . -B build-windows -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/llvm-mingw-x86_64.cmake \
#         -DLLVM_MINGW_ROOT=/opt/llvm-mingw \
#         -DCMAKE_PREFIX_PATH=/opt/Qt/6.11.3/llvm-mingw_64 \
#         -DQT_HOST_PATH=/opt/Qt/6.11.3/gcc_64
#
# Use the llvm-mingw release Qt was built with (Qt 6.11's is 20231128) and a Linux Qt of the
# same version for QT_HOST_PATH (moc, rcc and uic run on the build machine).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT LLVM_MINGW_ROOT)
    set(LLVM_MINGW_ROOT "$ENV{LLVM_MINGW_ROOT}")
endif()
if(NOT LLVM_MINGW_ROOT)
    message(FATAL_ERROR "Set LLVM_MINGW_ROOT to the llvm-mingw installation")
endif()
# try_compile projects re-read this file; pass the location on to them.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW_ROOT)

set(_triple x86_64-w64-mingw32)
set(CMAKE_C_COMPILER "${LLVM_MINGW_ROOT}/bin/${_triple}-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_ROOT}/bin/${_triple}-clang++")
set(CMAKE_RC_COMPILER "${LLVM_MINGW_ROOT}/bin/${_triple}-windres")

set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/${_triple}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
