#pragma once

#include "cpp_redis/builders/builder_iface.hpp"
#include "cpp_redis/builders/simple_string_builder.hpp"
#include "cpp_redis/replies/error_reply.hpp"

namespace cpp_redis {

namespace builders {

class error_builder : public builder_iface {
public:
  //! ctor & dtor
  error_builder(void) = default;
  ~error_builder(void) = default;

  //! copy ctor & assignment operator
  error_builder(const error_builder&) = delete;
  error_builder& operator=(const error_builder&) = delete;

public:
  //! builder_iface impl
  builder_iface& operator<<(std::string&);
  bool reply_ready(void) const;
  reply get_reply(void) const;

  //! getter
  const std::string& get_error(void) const;

private:
  simple_string_builder m_string_builder;
  replies::error_reply m_reply;
};

} //! builders

} //! cpp_redis
