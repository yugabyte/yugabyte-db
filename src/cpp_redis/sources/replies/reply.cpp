#include "cpp_redis/replies/reply.hpp"
#include "cpp_redis/replies/simple_string_reply.hpp"
#include "cpp_redis/replies/array_reply.hpp"
#include "cpp_redis/replies/bulk_string_reply.hpp"
#include "cpp_redis/replies/error_reply.hpp"
#include "cpp_redis/replies/integer_reply.hpp"
#include "cpp_redis/redis_error.hpp"

namespace cpp_redis {

namespace replies {

reply::reply(type reply_type)
: m_type(reply_type) {}

reply::type
reply::get_type(void) const {
  return m_type;
}

bool
reply::is_array(void) const {
  return m_type == type::array;
}

bool
reply::is_bulk_string(void) const {
  return m_type == type::bulk_string;
}

bool
reply::is_error(void) const {
  return m_type == type::error;
}

bool
reply::is_integer(void) const {
  return m_type == type::integer;
}

bool
reply::is_simple_string(void) const {
  return m_type == type::simple_string;
}

array_reply&
reply::as_array(void) {
  if (not is_array())
    throw redis_error("Reply is not an array");

  return *dynamic_cast<array_reply*>(this);
}

bulk_string_reply&
reply::as_bulk_string(void) {
  if (not is_bulk_string())
    throw redis_error("Reply is not a bulk string");

  return *dynamic_cast<bulk_string_reply*>(this);
}

error_reply&
reply::as_error(void) {
  if (not is_error())
    throw redis_error("Reply is not an error");

  return *dynamic_cast<error_reply*>(this);
}

integer_reply&
reply::as_integer(void) {
  if (not is_integer())
    throw redis_error("Reply is not an integer");

  return *dynamic_cast<integer_reply*>(this);
}

simple_string_reply&
reply::as_simple_string(void) {
  if (not is_simple_string())
    throw redis_error("Reply is not a simple string");

  return *dynamic_cast<simple_string_reply*>(this);
}

} //! replies

} //! cpp_redis
