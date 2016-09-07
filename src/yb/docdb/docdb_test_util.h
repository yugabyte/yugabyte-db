// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOCDB_TEST_UTIL_H_
#define YB_DOCDB_DOCDB_TEST_UTIL_H_

#include "yb/docdb/doc_key.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/subdocument.h"

#include <vector>
#include <random>

namespace yb {
namespace docdb {

using RandomNumberGenerator = std::mt19937_64;

// Maximum number of components in a randomly-generated DocKey.
static constexpr int kMaxNumRandomDocKeyParts = 10;

// Maximum number of subkeys in a randomly-geneerated SubDocKey.
static constexpr int kMaxNumRandomSubKeys = 10;

// Note: test data generator methods below are using a non-const reference for the random number
// generator for simplicity, even though it is against Google C++ Style Guide. If we used a pointer,
// we would have to invoke the RNG as (*rng)().

// Generate a random primitive value.
PrimitiveValue GenRandomPrimitiveValue(RandomNumberGenerator& rng);

// Generate a random sequence of primitive values.
std::vector<PrimitiveValue> GenRandomPrimitiveValues(
    RandomNumberGenerator& rng,
    int max_num = kMaxNumRandomDocKeyParts);

// Generate a "minimal" DocKey.
DocKey CreateMinimalDocKey(RandomNumberGenerator& rng, bool use_hash);

// Generate a random DocKey with up to the default number of components.
DocKey GenRandomDocKey(RandomNumberGenerator& rng, bool use_hash);

std::vector<DocKey> GenRandomDocKeys(RandomNumberGenerator& rng, bool use_hash, int num_keys) {
  std::vector<DocKey> result;
  result.push_back(CreateMinimalDocKey(rng, use_hash));
  for (int iteration = 0; iteration < num_keys; ++iteration) {
    result.push_back(GenRandomDocKey(rng, use_hash));
  }
  return result;
}

std::vector<SubDocKey> GenRandomSubDocKeys(RandomNumberGenerator& rng,
                                           bool use_hash,
                                           int num_keys) {
  std::vector<SubDocKey> result;
  result.push_back(SubDocKey(CreateMinimalDocKey(rng, use_hash), Timestamp(rng())));
  for (int iteration = 0; iteration < num_keys; ++iteration) {
    result.push_back(SubDocKey(GenRandomDocKey(rng, use_hash), Timestamp(rng())));
    for (int i = 0; i < rng() % (kMaxNumRandomSubKeys + 1); ++i) {
      result.back().AppendSubKeysAndTimestamps(GenRandomPrimitiveValue(rng), Timestamp(rng()));
    }
  }
  return result;
}

template<typename T>
const T& RandomElementOf(const std::vector<T>& v, RandomNumberGenerator& rng) {
  return v[rng() % v.size()];
}

}
}

#endif
