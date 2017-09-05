#pragma once

#include <string>
#include <memory>

#include "cpp_redis/reply.hpp"

namespace cpp_redis {

namespace builders {

//! interface inherited by all builders
class builder_iface {
public:
  virtual ~builder_iface(void) = default;

  //! take data as parameter which is consumed to build the reply
  //! every bytes used to build the reply must be removed from the buffer passed as parameter
  virtual builder_iface& operator<<(std::string&) = 0;

  //! return whether the reply could be built
  virtual bool reply_ready(void) const = 0;

  //! return reply object
  virtual reply get_reply(void) const = 0;
};

} //! builders

} //! cpp_redis
