#include "cpp_redis/replies/simple_string_reply.hpp"

namespace cpp_redis {

namespace replies {

simple_string_reply::simple_string_reply(const std::string& simple_string)
: reply(type::simple_string)
, m_str(simple_string) {}

const std::string&
simple_string_reply::str(void) const {
  return m_str;
}

void
simple_string_reply::str(const std::string& simple_string) {
  m_str = simple_string;
}

} //! replies

} //! cpp_redis
