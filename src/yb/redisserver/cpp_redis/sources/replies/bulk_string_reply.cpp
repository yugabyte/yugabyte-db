#include "cpp_redis/replies/bulk_string_reply.hpp"

namespace cpp_redis {

namespace replies {

bulk_string_reply::bulk_string_reply(bool is_null, const std::string& bulk_string)
: reply(type::bulk_string)
, m_str(bulk_string)
, m_is_null(is_null) {}

bool
bulk_string_reply::is_null(void) const {
  return m_is_null;
}

const std::string&
bulk_string_reply::str(void) const {
  return m_str;
}

void
bulk_string_reply::is_null(bool is_null) {
  m_is_null = is_null;
}

void
bulk_string_reply::str(const std::string& bulk_string) {
  m_str = bulk_string;
}

} //! replies

} //! cpp_redis
