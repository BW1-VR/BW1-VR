cmake_minimum_required(VERSION 3.20)
project(bwvr LANGUAGES C CXX)

# Black & White is a 32-bit process, so everything we inject must be 32-bit.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR
      "bwvr must be built for 32-bit. Configure with: cmake -A Win32")
endif()

# Static CRT everywhere, so the DLL drops beside a 2001 game with no
# runtime to install. This must be set before any target is declared.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The id of this build, for the first line of bwvr.log, so that the log of a
# tester says which build it is from. It is the short git commit, with a
# plus sign when tracked files have changes that are not committed.
# -DBWVR_BUILD_ID=... gives a different id. CMake reads the commit when it
# configures, and dev.ps1 configures before each build. The header is written
# again only when the id changes, so only the file that includes it is
# compiled again.
set(BWVR_BUILD_ID "" CACHE STRING
    "The build id for the log. Empty takes the git commit.")
set(BWVR_BUILD_ID_NOW "${BWVR_BUILD_ID}")
if(NOT BWVR_BUILD_ID_NOW)
  execute_process(
      COMMAND git rev-parse --short=7 HEAD
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE BWVR_BUILD_ID_NOW
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
  if(BWVR_BUILD_ID_NOW)
    execute_process(
        COMMAND git status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE BWVR_BUILD_DIRTY
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(BWVR_BUILD_DIRTY)
      string(APPEND BWVR_BUILD_ID_NOW "+")
    endif()
  endif()
endif()
if(NOT BWVR_BUILD_ID_NOW)
  set(BWVR_BUILD_ID_NOW "unknown")
endif()
configure_file("${CMAKE_SOURCE_DIR}/src/common/build_id.h.in"
               "${CMAKE_BINARY_DIR}/generated/build_id.h" @ONLY)

# The code of other projects, in the folder third_party of the repo. See
# third_party/README.md. A build from a git worktree gives the folder of the
# main checkout here: -DBWVR_THIRD_PARTY="C:/.../third_party". Do not put a
# directory link to it in a worktree: a recursive delete of the worktree
# follows the link and deletes the real folder.
set(BWVR_THIRD_PARTY "${CMAKE_SOURCE_DIR}/../third_party" CACHE PATH
    "The folder that holds minhook and OpenXR-SDK")
set(MINHOOK_DIR "${BWVR_THIRD_PARTY}/minhook")
if(NOT EXISTS "${MINHOOK_DIR}/src/hook.c")
  message(FATAL_ERROR
      "MinHook not found at ${MINHOOK_DIR}. It is part of the repo, in the "
      "folder third_party. Check that the clone is complete.")
endif()

add_library(minhook STATIC
    "${MINHOOK_DIR}/src/buffer.c"
    "${MINHOOK_DIR}/src/hook.c"
    "${MINHOOK_DIR}/src/trampoline.c"
    "${MINHOOK_DIR}/src/hde/hde32.c"
)
target_include_directories(minhook PUBLIC "${MINHOOK_DIR}/include")

add_subdirectory(src/probe)
add_subdirectory(src/vr)
add_subdirectory(src/xrtest)
add_subdirectory(src/mod)
add_subdirectory(src/launcher)
add_subdirectory(test/controller_math)
