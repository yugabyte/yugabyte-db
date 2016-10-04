#include "cpp_redis/reply.hpp"
#include "cpp_redis/replies/array_reply.hpp"
#include "cpp_redis/replies/bulk_string_reply.hpp"
#include "cpp_redis/replies/error_reply.hpp"
#include "cpp_redis/replies/integer_reply.hpp"
#include "cpp_redis/replies/simple_string_reply.hpp"

namespace cpp_redis {

reply::reply(void)
: m_type(type::null) {}

reply::reply(const replies::array_reply& array)
: m_type(type::array)
, m_replies(array.get_rows()) {}

reply::reply(const replies::bulk_string_reply& string)
: m_type(string.is_null() ? type::null : type::bulk_string)
, m_str(string.str()) {}

reply::reply(const replies::error_reply& string)
: m_type(type::error)
, m_str(string.str()) {}

reply::reply(const replies::integer_reply& integer)
: m_type(type::integer)
, m_int(integer.val()) {}

reply::reply(const replies::simple_string_reply& string)
: m_type(type::simple_string)
, m_str(string.str()) {}

reply&
reply::operator=(const replies::array_reply& array) {
  m_type = type::array;
  m_replies = array.get_rows();
  return *this;
}

reply&
reply::operator=(const replies::bulk_string_reply& string) {
  m_type = string.is_null() ? type::null : type::bulk_string;
  m_str = string.str();
  return *this;
}

reply&
reply::operator=(const replies::error_reply& string) {
  m_type = type::error;
  m_str = string.str();
  return *this;
}

reply&
reply::operator=(const replies::integer_reply& integer) {
  m_type = type::integer;
  m_str = integer.val();
  return *this;
}

reply&
reply::operator=(const replies::simple_string_reply& string) {
  m_type = type::simple_string;
  m_str = string.str();
  return *this;
}

bool
reply::is_array(void) const {
  return m_type == type::array;
}

bool
reply::is_string(void) const {
  return is_simple_string() or is_bulk_string() or is_error();
}

bool
reply::is_simple_string(void) const {
  return m_type == type::simple_string;
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
reply::is_null(void) const {
  return m_type == type::null;
}

const std::vector<reply>&
reply::as_array(void) const {
  return m_replies;
}

const std::string&
reply::as_string(void) const {
  return m_str;
}

int
reply::as_integer(void) const {
  return m_int;
}

reply::type
reply::get_type(void) const {
  return m_type;
}

} //! cpp_redis
