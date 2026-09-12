# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(COMPONENTS main) # "Trim" the build. Include the minimal set of components; main and anything it depends on.

# ---- Firmware identity ----------------------------------------------------
#
# Every image carries the version it was built as, and the commit it was
# built from, inside esp_app_desc_t - so a running device can answer "what
# are you?" over serial, on screen, and to CLAWBOX without anyone keeping a
# side table of which binary went where. The OTA manifest CLAWBOX publishes
# uses this same string as its "version" field, and ota_service.c compares
# the two literally; if they ever drift, updates simply stop happening rather
# than happening wrongly.
#
# Shape: <semver>+<short sha>[-dirty], e.g. "1.1.0+cb93a81" or
# "1.1.0+cb93a81-dirty". esp_app_desc_t allows 32 characters; this is 19 at
# the longest. Bump WORKSTATION_VERSION by hand - it describes the firmware's
# own capability, which git cannot infer.
set(WORKSTATION_VERSION "1.1.1")

execute_process(
    COMMAND git rev-parse --short=7 HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE WORKSTATION_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)

execute_process(
    COMMAND git status --porcelain --untracked-files=no
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE WORKSTATION_GIT_DIRTY
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)

if(NOT WORKSTATION_GIT_SHA)
    # Building from a tarball or a checkout without git. Better to say so than
    # to silently ship a version that claims a commit it does not have.
    set(WORKSTATION_GIT_SHA "nogit")
endif()

if(WORKSTATION_GIT_DIRTY)
    set(WORKSTATION_GIT_SHA "${WORKSTATION_GIT_SHA}-dirty")
endif()

# Re-run this configure step whenever the commit or the working tree changes.
# Without it CMake caches the version from whenever it last configured, and a
# build after a commit quietly ships an image labelled with the previous sha -
# which is worse than no version at all, because it looks trustworthy.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/.git/HEAD"
    "${CMAKE_CURRENT_LIST_DIR}/.git/index")

set(PROJECT_VER "${WORKSTATION_VERSION}+${WORKSTATION_GIT_SHA}")
message(STATUS "WORKSTATION firmware version: ${PROJECT_VER}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(status_deck)
