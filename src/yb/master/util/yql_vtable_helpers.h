// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_UTIL_YQL_VTABLE_HELPERS_H
#define YB_MASTER_UTIL_YQL_VTABLE_HELPERS_H

#include "yb/common/yql_value.h"
#include "yb/master/master.pb.h"
#include "yb/util/net/inetaddress.h"

namespace yb {
namespace master {
namespace util {

template<class T> struct GetValueHelper;

// In some cases we need to preserve the YQLValuePB.
template<> struct GetValueHelper<YQLValuePB> {

  static const YQLValuePB& Apply(const YQLValuePB& value_pb, const DataType data_type) {
    return value_pb;
  }
};

template<> struct GetValueHelper<std::string> {

  static YQLValuePB Apply(const std::string& strval, const DataType data_type) {
    YQLValuePB value_pb;
    switch (data_type) {
      case STRING:
        YQLValue::set_string_value(strval, &value_pb);
        break;
      case BINARY:
        YQLValue::set_binary_value(strval, &value_pb);
        break;
      default:
        LOG(ERROR) << "unexpected string type " << data_type;
        break;
    }
    return value_pb;
  }
};

// Need specialization for char[N] to handle strings literals.
template<std::size_t N> struct GetValueHelper<char[N]> {

  static YQLValuePB Apply(const char* strval, const DataType data_type) {
    return GetValueHelper<std::string>::Apply(strval, data_type);
  }
};

template<> struct GetValueHelper<int32_t> {

  static YQLValuePB Apply(const int32_t intval, const DataType data_type) {
    YQLValuePB value_pb;
    YQLValue::set_int32_value(intval, &value_pb);
    return value_pb;
  }
};

template<> struct GetValueHelper<InetAddress> {

  static YQLValuePB Apply(const InetAddress& inet_val, const DataType data_type) {
    YQLValuePB value_pb;
    YQLValue::set_inetaddress_value(inet_val, &value_pb);
    return value_pb;
  }
};

template<> struct GetValueHelper<Uuid> {

  static YQLValuePB Apply(const Uuid& uuid_val, const DataType data_type) {
    YQLValuePB value_pb;
    YQLValue::set_uuid_value(uuid_val, &value_pb);
    return value_pb;
  }
};

template<> struct GetValueHelper<bool> {

  static YQLValuePB Apply(const bool bool_val, const DataType data_type) {
    YQLValuePB value_pb;
    YQLValue::set_bool_value(bool_val, &value_pb);
    return value_pb;
  }
};

template<class T>
YQLValuePB GetValue(const T& t, DataType data_type) {
  typedef typename std::remove_cv<typename std::remove_reference<T>::type>::type CleanedT;
  return GetValueHelper<CleanedT>::Apply(t, data_type);
}

YQLValuePB GetTokensValue(size_t index, size_t node_count);

YQLValuePB GetReplicationValue(int replication_factor);

bool RemoteEndpointMatchesTServer(const TSInformationPB& ts_info,
                                  const InetAddress& remote_endpoint);

}  // namespace util
}  // namespace master
}  // namespace yb

#endif // YB_MASTER_UTIL_YQL_VTABLE_HELPERS_H
