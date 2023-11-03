// Copyright (c) YugaByte, Inc.
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
#include "yb/docdb/kv_debug.h"

#include <functional>
#include <string>

#include "yb/common/common.pb.h"

#include "yb/docdb/docdb-internal.h"
#include "yb/docdb/docdb_compaction_context.h"
#include "yb/docdb/docdb_types.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_kv_util.h"
#include "yb/dockv/intent.h"
#include "yb/dockv/packed_value.h"
#include "yb/dockv/schema_packing.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/fast_varint.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {
namespace docdb {

Result<std::string> DocDBKeyToDebugStr(
    Slice key_slice, StorageDbType db_type, dockv::HybridTimeRequired ht_required) {
  auto key_type = GetKeyType(key_slice, db_type);
  dockv::SubDocKey subdoc_key;
  switch (key_type) {
    case KeyType::kIntentKey: {
      auto decoded_intent_key = VERIFY_RESULT(dockv::DecodeIntentKey(key_slice));
      RETURN_NOT_OK(
          subdoc_key.FullyDecodeFromKeyWithOptionalHybridTime(decoded_intent_key.intent_prefix));
      return subdoc_key.ToString(dockv::AutoDecodeKeys::kTrue) + " " +
             ToString(decoded_intent_key.intent_types) + " " + decoded_intent_key.doc_ht.ToString();
    }
    case KeyType::kReverseTxnKey: {
      RETURN_NOT_OK(key_slice.consume_byte(dockv::KeyEntryTypeAsChar::kTransactionId));
      auto transaction_id = VERIFY_RESULT(DecodeTransactionId(&key_slice));
      auto doc_ht = VERIFY_RESULT_PREPEND(
          dockv::DecodeInvertedDocHt(key_slice),
          Format("Reverse txn record for: $0", transaction_id));
      return Format("TXN REV $0 $1", transaction_id, doc_ht);
    }
    case KeyType::kTransactionMetadata: {
      RETURN_NOT_OK(key_slice.consume_byte(dockv::KeyEntryTypeAsChar::kTransactionId));
      auto transaction_id = DecodeTransactionId(&key_slice);
      RETURN_NOT_OK(transaction_id);
      return Format("TXN META $0", *transaction_id);
    }
    case KeyType::kEmpty:
      FALLTHROUGH_INTENDED;
    case KeyType::kPlainSubDocKey:
      RETURN_NOT_OK_PREPEND(
          subdoc_key.FullyDecodeFrom(key_slice, ht_required),
          "Error: failed decoding SubDocKey " + FormatSliceAsStr(key_slice));
      return subdoc_key.ToString(dockv::AutoDecodeKeys::kTrue);
    case KeyType::kExternalIntents: {
      RETURN_NOT_OK(key_slice.consume_byte(dockv::KeyEntryTypeAsChar::kExternalTransactionId));
      auto transaction_id = VERIFY_RESULT(DecodeTransactionId(&key_slice));
      auto doc_hybrid_time = VERIFY_RESULT_PREPEND(
          dockv::DecodeInvertedDocHt(key_slice),
          Format("External txn record for: $0", transaction_id));
      return Format("TXN EXT $0 $1", transaction_id, doc_hybrid_time);
    }
  }
  return STATUS_FORMAT(Corruption, "Invalid KeyType: $0", yb::ToString(key_type));
}

namespace {

using PackingInfoPtr = std::shared_ptr<const dockv::SchemaPacking>;
using PackedRowToPackingInfoPtrFunc = std::function<Result<PackingInfoPtr>(Slice* packed_row)>;

Result<PackedRowToPackingInfoPtrFunc> GetPackedRowToPackingInfoPtrFunc(
    const Slice& key, KeyType key_type,
    SchemaPackingProvider* schema_packing_provider /*null ok*/) {
  // Extract cotable_id and/or colocation_id from key if present.
  Uuid cotable_id = Uuid::Nil();
  ColocationId colocation_id = kColocationIdNotSet;
  if (key_type == KeyType::kPlainSubDocKey || key_type == KeyType::kIntentKey) {
    dockv::SubDocKey subdoc_key;
    RETURN_NOT_OK(subdoc_key.FullyDecodeFrom(key, dockv::HybridTimeRequired::kFalse));
    auto& doc_key = subdoc_key.doc_key();
    if (doc_key.has_cotable_id()) {
      cotable_id = doc_key.cotable_id();
    }
    colocation_id = doc_key.colocation_id();
  }

  // We are done processing the key, now wait for the value.
  return [schema_packing_provider, cotable_id,
          colocation_id](Slice* packed_row) -> Result<PackingInfoPtr> {
    auto schema_version =
        narrow_cast<SchemaVersion>(VERIFY_RESULT(FastDecodeUnsignedVarInt(packed_row)));
    if (!schema_packing_provider) {
      return STATUS(NotFound, "No packing information available");
    }
    CompactionSchemaInfo compaction_schema_info;
    if (colocation_id != kColocationIdNotSet) {
      compaction_schema_info = VERIFY_RESULT(schema_packing_provider->ColocationPacking(
          colocation_id, schema_version, HybridTime::kMax));
    } else {
      compaction_schema_info = VERIFY_RESULT(
          schema_packing_provider->CotablePacking(cotable_id, schema_version, HybridTime::kMax));
    }
    return compaction_schema_info.schema_packing;
  };
}

Result<dockv::ValueControlFields> DecodeValueControlFields(dockv::PackedValueV1* value) {
  return dockv::ValueControlFields::Decode(&**value);
}

Result<dockv::ValueControlFields> DecodeValueControlFields(dockv::PackedValueV2* value) {
  return dockv::ValueControlFields();
}

template <class Decoder>
Result<std::string> PackedRowToString(const dockv::SchemaPacking& packing, Slice value_slice) {
  Decoder decoder(packing, value_slice.data());
  std::string result = "{";
  for (size_t i = 0; i != packing.columns(); ++i) {
    auto column_value = decoder.FetchValue(i);
    const auto& column_data = packing.column_packing_data(i);
    result += " ";
    result += column_data.id.ToString();
    result += ": ";
    auto column_control_fields = DecodeValueControlFields(&column_value);
    if (column_value.IsNull()) {
      result += "NULL";
    } else {
      auto pv = dockv::UnpackPrimitiveValue(column_value, column_data.data_type);
      if (!pv.ok()) {
        result += pv.status().ToString();
      } else {
        result += pv->ToString();
      }
    }
    result += column_control_fields.ToString();
  }
  result += " }";
  return result;
}

Result<std::string> DocDBValueToDebugStrInternal(
    KeyType key_type, Slice key, Slice value_slice,
    SchemaPackingProvider* schema_packing_provider /*null ok*/) {
  auto packed_row_to_packing_info_func =
      VERIFY_RESULT(GetPackedRowToPackingInfoPtrFunc(key, key_type, schema_packing_provider));
  std::string prefix;
  if (key_type == KeyType::kIntentKey) {
    auto txn_id_res = VERIFY_RESULT(dockv::DecodeTransactionIdFromIntentValue(&value_slice));
    prefix = Format("TransactionId($0) ", txn_id_res);
    if (value_slice.TryConsumeByte(dockv::ValueEntryTypeAsChar::kSubTransactionId)) {
      SubTransactionId subtransaction_id = Load<SubTransactionId, BigEndian>(value_slice.data());
      value_slice.remove_prefix(sizeof(SubTransactionId));
      prefix += Format("SubTransactionId($0) ", subtransaction_id);
    }
    if (!value_slice.empty()) {
      RETURN_NOT_OK(value_slice.consume_byte(dockv::ValueEntryTypeAsChar::kWriteId));
      if (value_slice.size() < sizeof(IntraTxnWriteId)) {
        return STATUS_FORMAT(Corruption, "Not enough bytes for write id: $0", value_slice.size());
      }
      auto write_id = BigEndian::Load32(value_slice.data());
      value_slice.remove_prefix(sizeof(write_id));
      prefix += Format("WriteId($0) ", write_id);
    }
  }

  // Empty values are allowed for weak intents.
  if (!value_slice.empty() || key_type != KeyType::kIntentKey) {
    dockv::Value v;
    auto control_fields = VERIFY_RESULT(dockv::ValueControlFields::Decode(&value_slice));
    auto packed_row_version = dockv::GetPackedRowVersion(value_slice);
    if (!packed_row_version) {
      RETURN_NOT_OK_PREPEND(
          v.Decode(value_slice, control_fields),
          Format("Error: failed to decode value $0", prefix));
      return prefix + v.ToString();
    } else {
      value_slice.consume_byte();
      auto packing = VERIFY_RESULT(packed_row_to_packing_info_func(&value_slice));
      switch (*packed_row_version) {
        case dockv::PackedRowVersion::kV1:
          prefix += VERIFY_RESULT(PackedRowToString<dockv::PackedRowDecoderV1>(
              *packing, value_slice));
          break;
        case dockv::PackedRowVersion::kV2:
          prefix += VERIFY_RESULT(PackedRowToString<dockv::PackedRowDecoderV2>(
              *packing, value_slice));
          break;
      }
      prefix += control_fields.ToString();
      return prefix;
    }
  } else {
    return prefix + "none";
  }
}

}  // namespace

Result<std::string> DocDBValueToDebugStr(
    Slice key, StorageDbType db_type, Slice value, SchemaPackingProvider* schema_packing_provider) {
  auto key_type = GetKeyType(key, db_type);
  return DocDBValueToDebugStr(key_type, key, value, schema_packing_provider);
}

Result<std::string> DocDBValueToDebugStr(
    KeyType key_type, Slice key, Slice value, SchemaPackingProvider* schema_packing_provider) {
  switch (key_type) {
    case KeyType::kTransactionMetadata: {
      TransactionMetadataPB metadata_pb;
      if (!metadata_pb.ParseFromArray(value.cdata(), narrow_cast<int>(value.size()))) {
        return STATUS_FORMAT(Corruption, "Bad metadata: $0", value.ToDebugHexString());
      }
      return ToString(VERIFY_RESULT(TransactionMetadata::FromPB(metadata_pb)));
    }
    case KeyType::kReverseTxnKey:
      return DocDBKeyToDebugStr(value, StorageDbType::kIntents);

    case KeyType::kEmpty:
      FALLTHROUGH_INTENDED;
    case KeyType::kIntentKey:
      FALLTHROUGH_INTENDED;
    case KeyType::kPlainSubDocKey: {
      return DocDBValueToDebugStrInternal(key_type, key, value, schema_packing_provider);
    }

    case KeyType::kExternalIntents: {
      std::vector<std::string> intents;
      dockv::SubDocKey sub_doc_key;
      RETURN_NOT_OK(value.consume_byte(dockv::ValueEntryTypeAsChar::kUuid));
      auto involved_tablet = VERIFY_RESULT(Uuid::FromSlice(value.Prefix(kUuidSize)));
      value.remove_prefix(kUuidSize);
      char header_byte = value.consume_byte();
      if (header_byte != dockv::KeyEntryTypeAsChar::kExternalIntents &&
          header_byte != dockv::KeyEntryTypeAsChar::kSubTransactionId) {
        return STATUS_FORMAT(
            Corruption, "Wrong first byte, expected $0 or $1 but found $2",
            static_cast<int>(dockv::KeyEntryTypeAsChar::kExternalIntents),
            static_cast<int>(dockv::KeyEntryTypeAsChar::kSubTransactionId), header_byte);
      }
      SubTransactionId subtransaction_id = kMinSubTransactionId;
      if (header_byte == dockv::KeyEntryTypeAsChar::kSubTransactionId) {
        subtransaction_id = Load<SubTransactionId, BigEndian>(value.data());
        value.remove_prefix(sizeof(SubTransactionId));
        RETURN_NOT_OK(value.consume_byte(dockv::KeyEntryTypeAsChar::kExternalIntents));
      }
      for (;;) {
        auto len = VERIFY_RESULT(FastDecodeUnsignedVarInt(&value));
        if (len == 0) {
          break;
        }
        Slice local_key = value.Prefix(len);
        value.remove_prefix(len);
        RETURN_NOT_OK(sub_doc_key.FullyDecodeFrom(local_key, dockv::HybridTimeRequired::kFalse));
        len = VERIFY_RESULT(FastDecodeUnsignedVarInt(&value));
        Slice local_value = value.Prefix(len);
        value.remove_prefix(len);
        intents.push_back(Format(
            "$0 -> $1",
            sub_doc_key,
            VERIFY_RESULT(DocDBValueToDebugStrInternal(
                KeyType::kPlainSubDocKey, local_key, local_value, schema_packing_provider))));
      }
      DCHECK(value.empty());
      if (header_byte == dockv::KeyEntryTypeAsChar::kSubTransactionId) {
        return Format(
            "IT $0 SubTransaction($1) $2", involved_tablet.ToHexString(), subtransaction_id,
            intents);
      } else {
        return Format("IT $0 $1", involved_tablet.ToHexString(), intents);
      }
    }
  }
  FATAL_INVALID_ENUM_VALUE(KeyType, key_type);
}

}  // namespace docdb
}  // namespace yb
