#include "cpp_redis/builders/bulk_string_builder.hpp"
#include "cpp_redis/redis_error.hpp"

namespace cpp_redis {

namespace builders {

bulk_string_builder::bulk_string_builder(void)
: m_str_size(0)
, m_str("")
, m_is_null(false)
, m_reply_ready(false) {}

void
bulk_string_builder::build_reply(void) {
  m_reply.str(m_str);
  m_reply.is_null(m_is_null);
  m_reply_ready = true;
}

bool
bulk_string_builder::fetch_size(std::string& buffer) {
  if (m_int_builder.reply_ready())
    return true;

  m_int_builder << buffer;
  if (not m_int_builder.reply_ready())
    return false;

  m_str_size = m_int_builder.get_integer();
  if (m_str_size == -1) {
    m_is_null = true;
    build_reply();
  }

  return true;
}

void
bulk_string_builder::fetch_str(std::string& buffer) {
  if (buffer.size() < static_cast<unsigned int>(m_str_size) + 2) // also wait for end sequence
    return ;

  if (buffer[m_str_size] != '\r' or buffer[m_str_size + 1] != '\n')
    throw redis_error("Wrong ending sequence");

  m_str = buffer.substr(0, m_str_size);
  buffer.erase(0, m_str_size + 2);
  build_reply();
}

builder_iface&
bulk_string_builder::operator<<(std::string& buffer) {
  if (m_reply_ready)
    return *this;

  //! if we don't have the size, try to get it with the current buffer
  if (not fetch_size(buffer) or m_reply_ready)
    return *this;

  fetch_str(buffer);

  return *this;
}

bool
bulk_string_builder::reply_ready(void) const {
  return m_reply_ready;
}

reply
bulk_string_builder::get_reply(void) const {
  return reply{ m_reply };
}

const std::string&
bulk_string_builder::get_bulk_string(void) const {
  return m_str;
}

bool
bulk_string_builder::is_null(void) const {
  return m_is_null;
}

} //! builders

} //! cpp_redis
