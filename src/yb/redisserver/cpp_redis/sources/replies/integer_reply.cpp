#include "cpp_redis/replies/integer_reply.hpp"

namespace cpp_redis {

namespace replies {

integer_reply::integer_reply(int nbr)
: reply(type::integer)
, m_nbr(nbr) {}

int
integer_reply::val(void) const {
  return m_nbr;
}

void
integer_reply::val(int nbr) {
  m_nbr = nbr;
}

} //! replies

} //! cpp_redis
