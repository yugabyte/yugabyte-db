#include "cpp_redis/replies/array_reply.hpp"
#include "cpp_redis/redis_error.hpp"
#include "cpp_redis/reply.hpp"

namespace cpp_redis {

namespace replies {

array_reply::array_reply(const std::vector<cpp_redis::reply>& rows)
: reply(type::array)
, m_rows(rows) {}

unsigned int
array_reply::size(void) const {
  return m_rows.size();
}

const std::vector<cpp_redis::reply>&
array_reply::get_rows(void) const {
  return m_rows;
}

const cpp_redis::reply&
array_reply::get(unsigned int idx) const {
  if (idx > size())
    throw redis_error("Index out of range");

  return *std::next(m_rows.begin(), idx);
}

const cpp_redis::reply&
array_reply::operator[](unsigned int idx) const {
  return get(idx);
}

void
array_reply::set_rows(const std::vector<cpp_redis::reply>& rows) {
  m_rows = rows;
}

void
array_reply::add_row(const cpp_redis::reply& row) {
  m_rows.push_back(row);
}

void
array_reply::operator<<(const cpp_redis::reply& row) {
  add_row(row);
}

} //! replies

} //! cpp_redis
