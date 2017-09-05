#include "cpp_redis/replies/error_reply.hpp"

namespace cpp_redis {

namespace replies {

error_reply::error_reply(const std::string& error)
: reply(type::error)
, m_error(error) {}

const std::string&
error_reply::str(void) const {
  return m_error;
}

void
error_reply::str(const std::string& error) {
  m_error = error;
}

} //! replies

} //! cpp_redis
