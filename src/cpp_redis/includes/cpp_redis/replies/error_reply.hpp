#pragma once

#include <string>

#include "cpp_redis/replies/reply.hpp"

namespace cpp_redis {

namespace replies {

class error_reply : public reply {
public:
  //! ctor & dtor
  error_reply(const std::string& error = "");
  ~error_reply(void) = default;

  //! copy ctor & assignment operator
  error_reply(const error_reply&) = default;
  error_reply& operator=(const error_reply&) = default;

public:
  //! getter
  const std::string& str(void) const;

  //! setter
  void str(const std::string& error);

private:
  std::string m_error;
};

} //! replies

} //! cpp_redis
