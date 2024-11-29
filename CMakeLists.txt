cmake_minimum_required(VERSION 3.12)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(vrtld C)

option(WITH_EXCEPTION_SUPPORT "Build with included exidx handler" ON)

set(SRC
  source/exports.c
  source/loader.c
  source/lookup.c
  source/reloc.c
  source/util.c
  source/vma.c
  source/vrtld.c
)

if(WITH_EXCEPTION_SUPPORT)
  list(APPEND SRC source/exception.c)
endif()

include_directories("${CMAKE_SOURCE_DIR}/include")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions("-DDEBUG")
endif()

add_library(${PROJECT_NAME} STATIC ${SRC})

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(FILES "${CMAKE_SOURCE_DIR}/include/vrtld.h" TYPE INCLUDE)
install(FILES "${CMAKE_SOURCE_DIR}/share/vrtld_shim.cmake" DESTINATION "$ENV{VITASDK}/share/")
