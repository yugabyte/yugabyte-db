#include "cpp_redis/builders/simple_string_builder.hpp"
#include "cpp_redis/redis_error.hpp"

namespace cpp_redis {

namespace builders {

simple_string_builder::simple_string_builder(void)
: m_str("")
, m_reply_ready(false) {}

builder_iface&
simple_string_builder::operator<<(std::string& buffer) {
  if (m_reply_ready)
    return *this;

  auto end_sequence = buffer.find("\r\n");
  if (end_sequence == std::string::npos)
    return *this;

  m_str = buffer.substr(0, end_sequence);
  m_reply.str(m_str);
  buffer.erase(0, end_sequence + 2);
  m_reply_ready = true;

  return *this;
}

bool
simple_string_builder::reply_ready(void) const {
  return m_reply_ready;
}

reply
simple_string_builder::get_reply(void) const {
  return reply{ m_reply };
}

const std::string&
simple_string_builder::get_simple_string(void) const {
  return m_str;
}

} //! builders

} //! cpp_redis
