#pragma once

#include <string>

#include "cpp_redis/replies/reply.hpp"

namespace cpp_redis {

namespace replies {

class simple_string_reply : public reply {
public:
  //! ctor & dtor
  simple_string_reply(const std::string& simple_string = "");
  ~simple_string_reply(void) = default;

  //! copy ctor & assignment operator
  simple_string_reply(const simple_string_reply&) = default;
  simple_string_reply& operator=(const simple_string_reply&) = default;

public:
  //! getter
  const std::string& str(void) const;

  //! setter
  void str(const std::string& simple_string);

private:
  std::string m_str;
};

} //! replies

} //! cpp_redis
