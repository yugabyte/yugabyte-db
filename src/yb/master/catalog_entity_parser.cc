// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/master/catalog_entity_parser.h"

#include "google/protobuf/text_format.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/catalog_entity_types.h"

#include "yb/util/pb_util.h"

namespace yb::master {

// -----------------------------------------------------------------------------
// SWITCH_FOR_EACH_CATALOG_ENTITY_TYPE
// -----------------------------------------------------------------------------
//
// Macro to switch case over all the SysRowEntryType and call the provided macro for each case.
//
// Ex:
// void PrintPBType(SysRowEntryType type) {
// #define HANDLE_CASE(entry_type, pb_type)
//   LOG(INFO) << "PB type for " << SysRowEntryType_Name(entry_type) << " is " << \'
//   BOOST_PP_STRINGIZE(pb_type);
//
//  SWITCH_FOR_EACH_CATALOG_ENTITY_TYPE(HANDLE_CASE, type);
//
// #undef HANDLE_CASE
// }
//
#define SWITCH_FOR_EACH_CATALOG_ENTITY_TYPE(_macro, _input_type) \
  static_assert( \
      std::is_same<decltype(_input_type), SysRowEntryType>::value, \
      "Expecting a SysRowEntryType type"); \
  switch (_input_type) { \
    BOOST_PP_SEQ_FOR_EACH(_SWITCH_CASE_FOR_CATALOG_ENTITY_TYPE, _macro, CATALOG_ENTITY_TYPE_MAP) \
    case SysRowEntryType::UNKNOWN: \
      return STATUS_FORMAT( \
          InvalidArgument, "Unsupported catalog entry type $0", \
          SysRowEntryType_Name(SysRowEntryType::UNKNOWN)); \
  } \
  return STATUS(InvalidArgument, Format("Unkown catalog entry type: $0", _input_type))

#define _GET_CATALOG_ENTITY_TYPE_FROM_ELEM(_entry_type, _pb_type) _entry_type

#define _SWITCH_CASE_FOR_CATALOG_ENTITY_TYPE(r, macro, elem) \
  case SysRowEntryType::_GET_CATALOG_ENTITY_TYPE_FROM_ELEM elem: { \
    macro elem \
  } break;

Result<std::unique_ptr<google::protobuf::Message>> DebugStringToCatalogEntityPB(
    SysRowEntryType type, const std::string& debug_string) {
#define HANDLE_CASE(entry_type, pb_type) \
  auto new_pb = std::make_unique<pb_type>(); \
  SCHECK_FORMAT( \
      google::protobuf::TextFormat::ParseFromString(debug_string, new_pb.get()), InvalidArgument, \
      "Failed to parse debug string into type $0", SysRowEntryType_Name(entry_type)); \
  return new_pb;

  SWITCH_FOR_EACH_CATALOG_ENTITY_TYPE(HANDLE_CASE, type);

#undef HANDLE_CASE
}

Result<std::unique_ptr<google::protobuf::Message>> SliceToCatalogEntityPB(
    SysRowEntryType type, const Slice& data) {
#define HANDLE_CASE(_entry_type, _pb_type) \
  return std::make_unique<_pb_type>(VERIFY_RESULT(pb_util::ParseFromSlice<_pb_type>(data)));

  SWITCH_FOR_EACH_CATALOG_ENTITY_TYPE(HANDLE_CASE, type);

#undef HANDLE_CASE
}

}  // namespace yb::master
