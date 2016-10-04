#pragma once

#include <vector>

#include "cpp_redis/replies/reply.hpp"

namespace cpp_redis {

class reply;

namespace replies {

class array_reply : public reply {
public:
  //! ctor & dtor
  array_reply(const std::vector<cpp_redis::reply>& rows = {});
  ~array_reply(void) = default;

  //! copy ctor & assignment operator
  array_reply(const array_reply&) = default;
  array_reply& operator=(const array_reply&) = default;

public:
  //! getters
  unsigned int size(void) const;
  const std::vector<cpp_redis::reply>& get_rows(void) const;

  const cpp_redis::reply& get(unsigned int idx) const;
  const cpp_redis::reply& operator[](unsigned int idx) const;

  //! setters
  void set_rows(const std::vector<cpp_redis::reply>& rows);
  void add_row(const cpp_redis::reply& row);
  void operator<<(const cpp_redis::reply& row);

private:
  std::vector<cpp_redis::reply> m_rows;
};

} //! replies

} //! cpp_redis
