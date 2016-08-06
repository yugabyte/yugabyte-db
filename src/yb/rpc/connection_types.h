// Copyright (c) YugaByte, Inc.

#ifndef YB_RPC_CONNECTION_TYPES_H_
#define YB_RPC_CONNECTION_TYPES_H_

#include "yb/util/enums.h"

namespace yb {
namespace rpc {

enum class ConnectionType : int16_t {
  YB,
  REDIS
};
}  // rpc
}  // yb

#endif //  YB_RPC_CONNECTION_TYPES_H_
