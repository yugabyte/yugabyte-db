#include "cpp_redis/builders/reply_builder.hpp"
#include "cpp_redis/builders/builders_factory.hpp"
#include "cpp_redis/redis_error.hpp"

namespace cpp_redis {

namespace builders {

reply_builder::reply_builder(void)
: m_builder(nullptr) {}

reply_builder&
reply_builder::operator<<(const std::string& data) {
  m_buffer += data;

  while (build_reply());

  return *this;
}

bool
reply_builder::build_reply(void) {
  if (not m_buffer.size())
    return false;

  if (not m_builder) {
    m_builder = create_builder(m_buffer.front());
    m_buffer.erase(0, 1);
  }

  *m_builder << m_buffer;

  if (m_builder->reply_ready()) {
    m_available_replies.push_back(m_builder->get_reply());
    m_builder = nullptr;

    return true;
  }

  return false;
}

void
reply_builder::operator>>(reply& reply) {
  reply = get_front();
}

const reply&
reply_builder::get_front(void) const {
  if (not reply_available())
    throw redis_error("No available reply");

  return m_available_replies.front();
}

void
reply_builder::pop_front(void) {
  if (not reply_available())
    throw redis_error("No available reply");

  m_available_replies.pop_front();
}

bool
reply_builder::reply_available(void) const {
  return m_available_replies.size() > 0;
}

} //! builders

} //! cpp_redis
