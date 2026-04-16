
# - Try to find the Prometheus C client library
#
#  PROM_FOUND - prom was successfully found
#  PROM_INCLUDE_DIR - prom include directory
#  PROM_LIBRARIES - prom libraries

find_path(PROM_INCLUDE_DIR NAMES prom.h)
find_library(PROM_LIBRARY prom)

find_package_handle_standard_args(PROM REQUIRED_VARS PROM_LIBRARY PROM_INCLUDE_DIR)

if (PROM_FOUND)
    message(STATUS "Found PROM: ${PROM_LIBRARY}")
endif()

find_path(PROMHTTP_INCLUDE_DIR NAMES promhttp.h)
find_library(PROMHTTP_LIBRARY promhttp)

find_package_handle_standard_args(PROMHTTP REQUIRED_VARS PROMHTTP_LIBRARY PROMHTTP_INCLUDE_DIR)

if (PROMHTTP_FOUND)
    message(STATUS "Found PROMHTTP: ${PROMHTTP_LIBRARY}")
endif()
