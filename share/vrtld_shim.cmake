# make cmake scripts believe we support dynamic linking
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS true)

# give it our pseudolinker
set(CMAKE_DL_LIBS "-lvrtld" CACHE STRING "" FORCE)

# vita.toolchain.cmake is missing these
set(CMAKE_SHARED_LINKER_FLAGS "-fPIC -shared -nostdlib -Wl,--unresolved-symbols=ignore-all ${CMAKE_SHARED_LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "-fPIC -shared -nostdlib -Wl,--unresolved-symbols=ignore-all ${CMAKE_MODULE_LINKER_FLAGS}")

# warn user about all of this
message("!! vrtld dynamic linking support enabled.")
