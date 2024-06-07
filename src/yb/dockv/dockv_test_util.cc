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

#include "yb/dockv/dockv_test_util.h"

#include "yb/common/ql_value.h"
#include "yb/common/value.messages.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/primitive_value.h"

#include "yb/util/random_util.h"

namespace yb::dockv {

namespace {

// Maximum number of subkeys in a randomly-generated SubDocKey.
constexpr int kMaxNumRandomSubKeys = 10;

}

ValueEntryType GenRandomPrimitiveValue(RandomNumberGenerator* rng, QLValuePB* holder) {
  static std::vector<std::string> kFruit = {
      "Apple",
      "Apricot",
      "Avocado",
      "Banana",
      "Bilberry",
      "Blackberry",
      "Blackcurrant",
      "Blood orange",
      "Blueberry",
      "Boysenberry",
      "Cantaloupe",
      "Cherimoya",
      "Cherry",
      "Clementine",
      "Cloudberry",
      "Coconut",
      "Cranberry",
      "Cucumber",
      "Currant",
      "Custard apple",
      "Damson",
      "Date",
      "Decaisnea Fargesii",
      "Dragonfruit",
      "Durian",
      "Elderberry",
      "Feijoa",
      "Fig",
      "Goji berry",
      "Gooseberry",
      "Grape",
      "Grapefruit",
      "Guava",
      "Honeyberry",
      "Honeydew",
      "Huckleberry",
      "Jabuticaba",
      "Jackfruit",
      "Jambul",
      "Jujube",
      "Juniper berry",
      "Kiwifruit",
      "Kumquat",
      "Lemon",
      "Lime",
      "Longan",
      "Loquat",
      "Lychee",
      "Mandarine",
      "Mango",
      "Marionberry",
      "Melon",
      "Miracle fruit",
      "Mulberry",
      "Nance",
      "Nectarine",
      "Olive",
      "Orange",
      "Papaya",
      "Passionfruit",
      "Peach",
      "Pear",
      "Persimmon",
      "Physalis",
      "Pineapple",
      "Plantain",
      "Plum",
      "Plumcot (or Pluot)",
      "Pomegranate",
      "Pomelo",
      "Prune (dried plum)",
      "Purple mangosteen",
      "Quince",
      "Raisin",
      "Rambutan",
      "Raspberry",
      "Redcurrant",
      "Salak",
      "Salal berry",
      "Salmonberry",
      "Satsuma",
      "Star fruit",
      "Strawberry",
      "Tamarillo",
      "Tamarind",
      "Tangerine",
      "Tomato",
      "Ugli fruit",
      "Watermelon",
      "Yuzu"
  };
  switch ((*rng)() % 6) {
    case 0:
      *holder = QLValue::Primitive(static_cast<int64_t>((*rng)()));
      return ValueEntryType::kInvalid;
    case 1: {
      std::string s;
      for (size_t j = 0; j < (*rng)() % 50; ++j) {
        s.push_back((*rng)() & 0xff);
      }
      *holder = QLValue::Primitive(s);
      return ValueEntryType::kInvalid;
    }
    case 2: return ValueEntryType::kNullLow;
    case 3: return ValueEntryType::kTrue;
    case 4: return ValueEntryType::kFalse;
    case 5: {
      *holder = QLValue::Primitive(kFruit[(*rng)() % kFruit.size()]);
      return ValueEntryType::kInvalid;
    }
  }
  LOG(FATAL) << "Should never get here";
  return ValueEntryType::kNullLow;  // to make the compiler happy
}

PrimitiveValue GenRandomPrimitiveValue(RandomNumberGenerator* rng) {
  QLValuePB value_holder;
  auto custom_value_type = GenRandomPrimitiveValue(rng, &value_holder);
  if (custom_value_type != ValueEntryType::kInvalid) {
    return PrimitiveValue(custom_value_type);
  }
  return PrimitiveValue::FromQLValuePB(value_holder);
}

DocKey CreateMinimalDocKey(RandomNumberGenerator* rng, UseHash use_hash) {
  return use_hash
      ? DocKey(static_cast<DocKeyHash>((*rng)()), KeyEntryValues(), KeyEntryValues())
      : DocKey();
}

DocKey GenRandomDocKey(RandomNumberGenerator* rng, UseHash use_hash) {
  if (use_hash) {
    return DocKey(
        static_cast<uint32_t>((*rng)()),  // this is just a random value, not a hash function result
        GenRandomKeyEntryValues(rng),
        GenRandomKeyEntryValues(rng));
  } else {
    return DocKey(GenRandomKeyEntryValues(rng));
  }
}

std::vector<DocKey> GenRandomDocKeys(RandomNumberGenerator* rng, UseHash use_hash, int num_keys) {
  std::vector<DocKey> result;
  result.push_back(CreateMinimalDocKey(rng, use_hash));
  for (int iteration = 0; iteration < num_keys; ++iteration) {
    result.push_back(GenRandomDocKey(rng, use_hash));
  }
  return result;
}

std::vector<SubDocKey> GenRandomSubDocKeys(
    RandomNumberGenerator* rng, UseHash use_hash, int num_keys) {
  std::vector<SubDocKey> result;
  result.push_back(SubDocKey(CreateMinimalDocKey(rng, use_hash), HybridTime((*rng)())));
  for (int iteration = 0; iteration < num_keys; ++iteration) {
    result.push_back(SubDocKey(GenRandomDocKey(rng, use_hash)));
    for (size_t i = 0; i < (*rng)() % (kMaxNumRandomSubKeys + 1); ++i) {
      result.back().AppendSubKeysAndMaybeHybridTime(GenRandomKeyEntryValue(rng));
    }
    const IntraTxnWriteId write_id = static_cast<IntraTxnWriteId>(
        (*rng)() % 2 == 0 ? 0 : (*rng)() % 1000000);
    result.back().set_hybrid_time(DocHybridTime(HybridTime((*rng)()), write_id));
  }
  return result;
}

KeyEntryValue GenRandomKeyEntryValue(RandomNumberGenerator* rng) {
  QLValuePB value_holder;
  auto custom_value_type = GenRandomPrimitiveValue(rng, &value_holder);
  if (custom_value_type != ValueEntryType::kInvalid) {
    return KeyEntryValue(static_cast<KeyEntryType>(custom_value_type));
  }
  return KeyEntryValue::FromQLValuePB(value_holder, SortingType::kNotSpecified);
}

// Generate a vector of random primitive values.
std::vector<KeyEntryValue> GenRandomKeyEntryValues(
    RandomNumberGenerator* rng, int max_num) {
  std::vector<KeyEntryValue> result;
  for (size_t i = 0; i < (*rng)() % (max_num + 1); ++i) {
    result.push_back(GenRandomKeyEntryValue(rng));
  }
  return result;
}

QLValuePB RandomQLValue(DataType type) {
  switch (type) {
    case DataType::BOOL: {
      QLValuePB result;
      result.set_bool_value(RandomUniformBool());
      return result;
    }
    case DataType::INT8: {
      QLValuePB result;
      result.set_int8_value(RandomUniformInt<int8_t>());
      return result;
    }
    case DataType::INT16: {
      QLValuePB result;
      result.set_int16_value(RandomUniformInt<int16_t>());
      return result;
    }
    case DataType::INT32:
      return QLValue::Primitive(RandomUniformInt<int32_t>());
    case DataType::INT64:
      return QLValue::Primitive(RandomUniformInt<int64_t>());
    case DataType::STRING:
      return QLValue::Primitive(RandomHumanReadableString(RandomUniformInt(0, 32)));
    case DataType::DOUBLE:
      return QLValue::Primitive(RandomUniformReal<double>());
    default:
      CHECK(false) << "Not supported data type: " << type;
  }
}

}  // namespace yb::dockv
