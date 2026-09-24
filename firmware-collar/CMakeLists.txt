# CowNect collar firmware - ESP-IDF project root.
#
# Pinned framework: ESP-IDF v5.5.5 (see README.md and main/idf_component.yml).
# Do not migrate to ESP-IDF v6.x without an explicit project decision.

cmake_minimum_required(VERSION 3.16)

# Components are grouped in sub-folders for readability. Each leaf folder that
# contains a CMakeLists.txt is an ESP-IDF component.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/drivers"
    "${CMAKE_CURRENT_LIST_DIR}/components/communication"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(DEFINED IDF_VERSION_MAJOR)
    set(_cownect_idf "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}")
    if(NOT _cownect_idf VERSION_EQUAL "5.5.5")
        message(WARNING "CowNect firmware is pinned to ESP-IDF v5.5.5; building with ${_cownect_idf}. "
                        "ADC continuous / I2C master APIs were validated only against v5.5.5.")
    endif()
endif()

project(cownect_collar)
