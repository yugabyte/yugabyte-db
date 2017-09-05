#pragma once

#include "cpp_redis/builders/builder_iface.hpp"
#include "cpp_redis/builders/integer_builder.hpp"
#include "cpp_redis/replies/bulk_string_reply.hpp"

namespace cpp_redis {

namespace builders {

class bulk_string_builder : public builder_iface {
public:
  //! ctor & dtor
  bulk_string_builder(void);
  ~bulk_string_builder(void) = default;

  //! copy ctor & assignment operator
  bulk_string_builder(const bulk_string_builder&) = delete;
  bulk_string_builder& operator=(const bulk_string_builder&) = delete;

public:
  //! builder_iface impl
  builder_iface& operator<<(std::string&);
  bool reply_ready(void) const;
  reply get_reply(void) const;

  //! getter
  const std::string& get_bulk_string(void) const;
  bool is_null(void) const;

private:
  void build_reply(void);
  bool fetch_size(std::string& str);
  void fetch_str(std::string& str);

private:
  //! used to get bulk string size
  integer_builder m_int_builder;

  int m_str_size;
  std::string m_str;
  bool m_is_null;

  bool m_reply_ready;
  replies::bulk_string_reply m_reply;
};

} //! builders

} //! cpp_redis
