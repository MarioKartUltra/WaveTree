# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Abraham Joseph Okayli Masaryk
# Third-party sources: JUCE and foleys_gui_magic.
#
# A local checkout wins, so one JUCE can serve several projects; otherwise the
# pinned version is fetched at configure time (network, once). Either way the
# tree is the one the plugin was released from.
#
#   JUCE 8.0.7         -DJUCE_DIR=<path>    > external/JUCE              > fetched
#   foleys_gui_magic   -DFOLEYS_DIR=<path>  > external/foleys_gui_magic  > fetched
#
# The foleys source is the author's fork, branch v2-local-fixes: upstream
# 1a4ffc3 plus six fixes these layouts depend on (toggle state, View
# visibility, root background colour, UTF-8 captions).
#
# A project that builds without foleys sets THIRDPARTY_WITH_FOLEYS OFF first.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
if(NOT DEFINED THIRDPARTY_WITH_FOLEYS)
    set(THIRDPARTY_WITH_FOLEYS ON)
endif()

if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE")
else()
    # Populate only (the SOURCE_SUBDIR does not exist): JUCE is added below
    # with the same binary directory a local checkout gets.
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.7
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
        SOURCE_SUBDIR  cmake-not-used)
    FetchContent_MakeAvailable(juce)
    set(JUCE_DIR "${juce_SOURCE_DIR}")
endif()
message(STATUS "${PROJECT_NAME}: JUCE at ${JUCE_DIR}")
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

if(THIRDPARTY_WITH_FOLEYS)
    if(DEFINED FOLEYS_DIR AND EXISTS "${FOLEYS_DIR}/modules/foleys_gui_magic/foleys_gui_magic.h")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/foleys_gui_magic/modules/foleys_gui_magic/foleys_gui_magic.h")
        set(FOLEYS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/foleys_gui_magic")
    else()
        FetchContent_Declare(foleys
            GIT_REPOSITORY https://github.com/MarioKartUltra/foleys_gui_magic.git
            GIT_TAG        46d42c556012d347f5ab815602a7c03e1a396ec8
            GIT_PROGRESS   TRUE
            SOURCE_SUBDIR  cmake-not-used)
        FetchContent_MakeAvailable(foleys)
        set(FOLEYS_DIR "${foleys_SOURCE_DIR}")
    endif()
    message(STATUS "${PROJECT_NAME}: foleys_gui_magic at ${FOLEYS_DIR}")
    # Only the JUCE module: foleys' own CMake would fetch a second JUCE.
    juce_add_module("${FOLEYS_DIR}/modules/foleys_gui_magic")
endif()
