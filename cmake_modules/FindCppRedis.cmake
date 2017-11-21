find_path(CPP_REDIS_INCLUDE_DIR cpp_redis/cpp_redis
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(CPP_REDIS_STATIC_LIB libcpp_redis.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CPP_REDIS REQUIRED_VARS
  CPP_REDIS_STATIC_LIB CPP_REDIS_INCLUDE_DIR)
