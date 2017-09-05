#pragma once

#include <stdexcept>

namespace cpp_redis {

class redis_error : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
  using std::runtime_error::what;
};

} //! cpp_redis
