#include "cpp_redis/builders/builders_factory.hpp"
#include "cpp_redis/builders/array_builder.hpp"
#include "cpp_redis/builders/error_builder.hpp"
#include "cpp_redis/builders/integer_builder.hpp"
#include "cpp_redis/builders/bulk_string_builder.hpp"
#include "cpp_redis/builders/simple_string_builder.hpp"
#include "cpp_redis/redis_error.hpp"

namespace cpp_redis {

namespace builders {

std::unique_ptr<builder_iface>
create_builder(char id) {
  switch (id) {
  case '+':
    return std::unique_ptr<simple_string_builder>{ new simple_string_builder() };
  case '-':
    return std::unique_ptr<error_builder>{ new error_builder() };
  case ':':
    return std::unique_ptr<integer_builder>{ new integer_builder() };
  case '$':
    return std::unique_ptr<bulk_string_builder>{ new bulk_string_builder() };
  case '*':
    return std::unique_ptr<array_builder>{ new array_builder() };
  default:
    throw redis_error("Invalid data");
  }
}

} //! builders

} //! cpp_redis
