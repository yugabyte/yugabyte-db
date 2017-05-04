// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_UTIL_YQL_VTABLE_HELPERS_H
#define YB_MASTER_UTIL_YQL_VTABLE_HELPERS_H

namespace yb {
namespace master {
namespace util {

inline YQLValuePB GetStringValue(const std::string& strval) {
  YQLValuePB value_pb;
  YQLValue::set_string_value(strval, &value_pb);
  return value_pb;
}

inline YQLValuePB GetIntValue(const int32_t intval) {
  YQLValuePB value_pb;
  YQLValue::set_int32_value(intval, &value_pb);
  return value_pb;
}

inline YQLValuePB GetInetValue(const InetAddress& inet_val) {
  YQLValuePB value_pb;
  YQLValue::set_inetaddress_value(inet_val, &value_pb);
  return value_pb;
}

inline YQLValuePB GetUuidValue(const Uuid& uuid_val) {
  YQLValuePB value_pb;
  YQLValue::set_uuid_value(uuid_val, &value_pb);
  return value_pb;
}

inline YQLValuePB GetBoolValue(const bool bool_val) {
  YQLValuePB value_pb;
  YQLValue::set_bool_value(bool_val, &value_pb);
  return value_pb;
}

// TODO (mihnea) when partitioning issue is solved this should take arguments and return the
// appropriate result for each node.
inline YQLValuePB GetTokensValue() {
  YQLValuePB value_pb;
  YQLValue::set_set_value(&value_pb);
  YQLValuePB *token = YQLValue::add_set_elem(&value_pb);
  token->set_string_value("0");
  return value_pb;
}

}  // namespace util
}  // namespace master
}  // namespace yb

#endif // YB_MASTER_UTIL_YQL_VTABLE_HELPERS_H
