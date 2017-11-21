find_path(TACOPIE_INCLUDE_DIR tacopie/tacopie
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(TACOPIE_STATIC_LIB libtacopie.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(TACOPIE REQUIRED_VARS
  TACOPIE_STATIC_LIB TACOPIE_INCLUDE_DIR)
