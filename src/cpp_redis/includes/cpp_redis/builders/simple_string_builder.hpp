#pragma once

#include <string>

#include "cpp_redis/builders/builder_iface.hpp"
#include "cpp_redis/replies/simple_string_reply.hpp"

namespace cpp_redis {

namespace builders {

class simple_string_builder : public builder_iface {
public:
  //! ctor & dtor
  simple_string_builder(void);
  ~simple_string_builder(void) = default;

  //! copy ctor & assignment operator
  simple_string_builder(const simple_string_builder&) = delete;
  simple_string_builder& operator=(const simple_string_builder&) = delete;

public:
  //! builder_iface impl
  builder_iface& operator<<(std::string&);
  bool reply_ready(void) const;
  reply get_reply(void) const;

  //! getter
  const std::string& get_simple_string(void) const;

private:
  std::string m_str;
  bool m_reply_ready;

  replies::simple_string_reply m_reply;
};

} //! builders

} //! cpp_redis
