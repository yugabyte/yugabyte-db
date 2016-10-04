#pragma once

#include <string>
#include <vector>

namespace cpp_redis {

namespace replies {
class array_reply;
class bulk_string_reply;
class error_reply;
class integer_reply;
class simple_string_reply;
} //! replies

class reply {
public:
  //! type of reply
  enum class type {
    array,
    bulk_string,
    error,
    integer,
    simple_string,
    null
  };

public:
  //! ctors
  reply(void);
  explicit reply(const replies::array_reply& array);
  explicit reply(const replies::bulk_string_reply& string);
  explicit reply(const replies::error_reply& string);
  explicit reply(const replies::integer_reply& integer);
  explicit reply(const replies::simple_string_reply& string);

  //! dtors & copy ctor & assignment operator
  ~reply(void) = default;
  reply(const reply&) = default;
  reply& operator=(const reply&) = default;

  //! custom assignment operators
  reply& operator=(const replies::array_reply& array);
  reply& operator=(const replies::bulk_string_reply& string);
  reply& operator=(const replies::error_reply& string);
  reply& operator=(const replies::integer_reply& integer);
  reply& operator=(const replies::simple_string_reply& string);

public:
  bool is_array(void) const;
  bool is_string(void) const;
  bool is_simple_string(void) const;
  bool is_bulk_string(void) const;
  bool is_error(void) const;
  bool is_integer(void) const;
  bool is_null(void) const;

  const std::vector<reply>& as_array(void) const;
  const std::string& as_string(void) const;
  int as_integer(void) const;

  type get_type(void) const;

private:
  type m_type;
  std::vector<reply> m_replies;
  std::string m_str;
  int m_int;
};

} //! cpp_redis
