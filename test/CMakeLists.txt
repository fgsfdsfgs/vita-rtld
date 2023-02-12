cmake_minimum_required(VERSION 3.12)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(vrtld_test C)

include("$ENV{VITASDK}/share/vrtld_shim.cmake")

set(APP_SRC "${CMAKE_SOURCE_DIR}/source/main.c")
set(LIB_SRC "${CMAKE_SOURCE_DIR}/source/lib.c")

add_library(testlib SHARED ${LIB_SRC})
target_compile_options(testlib PRIVATE -Wall -nostdlib -shared -fPIC -Wl,--unresolved-symbols=ignore-all)

add_executable(testapp.elf ${APP_SRC})
target_compile_options(testapp.elf PRIVATE -Wall)
target_link_libraries(testapp.elf
  -Wl,--whole-archive pthread m -Wl,--no-whole-archive
  vrtld
  taihen_stub 
  SceKernelModulemgr_stub
  SceSblSsMgr_stub
  kubridge_stub
  SceVshBridge_stub
)

vita_create_self(testapp.self
  testapp.elf
  UNSAFE
  GEN_EXPORTS testapp.yml
)

vita_create_vpk(testapp.vpk
  "RTLD69696"
  testapp.self
  NAME "vrtld_test"
  VERSION "01.00"
  FILE sce_sys sce_sys
  FILE "${CMAKE_BINARY_DIR}/libtestlib.so" "libtestlib.so"
)
