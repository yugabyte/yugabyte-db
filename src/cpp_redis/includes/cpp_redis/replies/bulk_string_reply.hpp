#pragma once

#include <string>

#include "cpp_redis/replies/reply.hpp"

namespace cpp_redis {

namespace replies {

class bulk_string_reply : public reply {
public:
  //! ctor & dtor
  bulk_string_reply(bool is_null = false, const std::string& bulk_string = "");
  ~bulk_string_reply(void) = default;

  //! copy ctor & assignment operator
  bulk_string_reply(const bulk_string_reply&) = default;
  bulk_string_reply& operator=(const bulk_string_reply&) = default;

public:
  //! getters
  bool is_null(void) const;
  const std::string& str(void) const;

  //! setters
  void is_null(bool is_null);
  void str(const std::string& bulk_string);

private:
  std::string m_str;
  bool m_is_null;
};

} //! replies

} //! cpp_redis
