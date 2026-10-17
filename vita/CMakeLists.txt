cmake_minimum_required(VERSION 3.18)
set(BOREALIS_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/vendor/borealis/library")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(BOREALIS_APPLICATION "${BOREALIS_LIBRARY}/lib/core/application.cpp")
set(BOREALIS_FOCUS_FIX "${CMAKE_CURRENT_BINARY_DIR}/patched-borealis/application.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${BOREALIS_APPLICATION}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/prepare-borealis.py")
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/prepare-borealis.py"
  "${BOREALIS_APPLICATION}" "${BOREALIS_FOCUS_FIX}"
  RESULT_VARIABLE BOREALIS_PATCH_RESULT)
if(NOT BOREALIS_PATCH_RESULT EQUAL 0)
  message(FATAL_ERROR "Cannot prepare the Borealis focus fix")
endif()
option(MOONFIN_HOST_TESTS "Build portable protocol tests only" OFF)
if(MOONFIN_HOST_TESTS)
  project(MoonfinVitaTests LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 17)
  enable_testing()
  find_package(CURL REQUIRED)
  find_package(Threads REQUIRED)
  add_executable(moonfin-tests tests/client_test.cpp src/client.cpp src/http.cpp src/runtime.cpp)
  target_include_directories(moonfin-tests PRIVATE include "${BOREALIS_LIBRARY}/include/borealis/extern")
  target_link_libraries(moonfin-tests PRIVATE CURL::libcurl Threads::Threads)
  target_compile_options(moonfin-tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME moonfin-protocol COMMAND moonfin-tests)
  add_executable(moonfin-timeline-tests tests/timeline_test.cpp)
  target_include_directories(moonfin-timeline-tests PRIVATE include)
  target_compile_options(moonfin-timeline-tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME moonfin-timeline COMMAND moonfin-timeline-tests)
  return()
endif()

set(PLATFORM_PSV ON CACHE BOOL "" FORCE)
set(USE_GXM ON CACHE BOOL "" FORCE)
set(USE_VITA_SHARK OFF CACHE BOOL "" FORCE)
set(USE_LIBROMFS OFF CACHE BOOL "" FORCE)
include("${BOREALIS_LIBRARY}/cmake/commonOption.cmake")
include("${BOREALIS_LIBRARY}/cmake/toolchain.cmake")
project(MoonfinVita VERSION 0.1.3 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_subdirectory("${BOREALIS_LIBRARY}" borealis EXCLUDE_FROM_ALL)
# Compile the reviewed focus override; leave the pinned submodule untouched.
get_target_property(BOREALIS_SOURCES borealis SOURCES)
if(NOT "${BOREALIS_APPLICATION}" IN_LIST BOREALIS_SOURCES)
  message(FATAL_ERROR "Borealis application source layout changed")
endif()
list(REMOVE_ITEM BOREALIS_SOURCES "${BOREALIS_APPLICATION}")
list(APPEND BOREALIS_SOURCES "${BOREALIS_FOCUS_FIX}")
set_property(TARGET borealis PROPERTY SOURCES "${BOREALIS_SOURCES}")
target_compile_options(borealis PRIVATE -g1)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED mpv)
pkg_check_modules(CURL REQUIRED libcurl)
# Borealis links pthread with --whole-archive to keep its Vita initialization.
# A preceding normal pthread link from pkg-config would extract the same objects twice.
list(REMOVE_ITEM MPV_STATIC_LIBRARIES pthread)
list(REMOVE_ITEM CURL_STATIC_LIBRARIES pthread)
file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(MoonfinVita ${SOURCES})
target_include_directories(MoonfinVita PRIVATE include ${MPV_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})
target_link_directories(MoonfinVita PRIVATE ${MPV_STATIC_LIBRARY_DIRS} ${CURL_STATIC_LIBRARY_DIRS})
target_compile_options(MoonfinVita PRIVATE -g1 -Wall -Wextra -Wno-unused-parameter -ffunction-sections -fdata-sections)
target_link_libraries(MoonfinVita PRIVATE borealis ${MPV_STATIC_LIBRARIES} ${CURL_STATIC_LIBRARIES} SceAppMgr_stub)
target_link_options(MoonfinVita PRIVATE -Wl,--gc-sections)
add_custom_command(TARGET MoonfinVita POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:MoonfinVita>"
          "${CMAKE_CURRENT_BINARY_DIR}/MoonfinVita.elf")

# Merge framework resources at build time; keep app assets sourced from Moonfin Core.
set(PACK_RES "${CMAKE_CURRENT_BINARY_DIR}/resources")
file(COPY vendor/borealis/resources/ DESTINATION "${PACK_RES}")
file(COPY resources/ DESTINATION "${PACK_RES}")
configure_file(THIRD_PARTY_NOTICES.md "${PACK_RES}/MOONFIN_NOTICES.txt" COPYONLY)
configure_file(LICENSE "${PACK_RES}/licenses/Native-GPL-3.0.txt" COPYONLY)
configure_file(../LICENSE "${PACK_RES}/licenses/Moonfin-Core-GPL-2.0.txt" COPYONLY)
file(COPY ../assets/fonts/Roboto-LICENSE.txt licenses/ DESTINATION "${PACK_RES}/licenses")
file(COPY ../assets/fonts/Roboto-Regular.ttf DESTINATION "${PACK_RES}/font")
configure_file(../assets/fonts/Roboto-Regular.ttf "${PACK_RES}/font/switch_font.ttf" COPYONLY)
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")
vita_create_self(MoonfinVita.self MoonfinVita UNSAFE NOASLR)
vita_create_vpk(MoonfinVita.vpk MFVITA001 MoonfinVita.self
  VERSION 00.13 NAME "Moonfin Vita"
  FILE "${PACK_RES}" resources
       "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png" sce_sys/icon0.png
       "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml" sce_sys/livearea/contents/template.xml
       "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/bg.png" sce_sys/livearea/contents/bg.png
       "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/startup.png" sce_sys/livearea/contents/startup.png
)
