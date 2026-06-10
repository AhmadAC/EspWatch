cmake_minimum_required(VERSION 3.16)

# Register the "apps" directory using an absolute path
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/apps")

# Include the main ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(Smartwatch_OS)