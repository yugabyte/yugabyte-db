//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_REDISSERVER_REDIS_FWD_H
#define YB_REDISSERVER_REDIS_FWD_H

#include <boost/container/container_fwd.hpp>

namespace yb {

class Slice;

namespace redisserver {

// Command arguments. The memory is owned by RedisInboundCall.
using RedisClientCommand = boost::container::small_vector<Slice, 8>;
using RedisClientBatch = boost::container::small_vector<RedisClientCommand, 16>;

} // namespace redisserver
} // namespace yb

#endif // YB_REDISSERVER_REDIS_FWD_H
