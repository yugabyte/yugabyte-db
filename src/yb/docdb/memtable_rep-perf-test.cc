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

// Compares memtable representations on DocDB keys.

#include <chrono>
#include <random>

#include <gtest/gtest.h>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/rocksdb_writer.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_kv_util.h"
#include "yb/dockv/intent.h"

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/memtable.h"
#include "yb/rocksdb/db/writebuffer.h"
#include "yb/rocksdb/immutable_options.h"
#include "yb/rocksdb/memtablerep.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/util/arena.h"
#include "yb/rocksdb/util/mutable_cf_options.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/random_util.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

DEFINE_NON_RUNTIME_uint64(memtable_perf_entries,
                          yb::RegularBuildVsDebugVsSanitizers(1000000, 100000, 10000),
                          "Entries per memtable.");
DEFINE_NON_RUNTIME_uint64(memtable_perf_lookups,
                          yb::RegularBuildVsDebugVsSanitizers(1000000, 100000, 10000),
                          "Seeks per lookup measurement.");
DEFINE_NON_RUNTIME_uint64(memtable_perf_readers, 4, "Reader threads during concurrent inserts.");
DEFINE_NON_RUNTIME_string(memtable_perf_workloads, "",
                          "Comma separated workloads to run, all if empty.");

namespace yb::docdb {

namespace {

constexpr size_t kValueSize = 64;
// First tablet of a table with three hash tablets.
constexpr uint16_t kTabletHashEnd = 0x5555;
constexpr size_t kScanLength = 100;
constexpr size_t kRowsPerTransaction = 16;

struct Workload {
  std::string name;
  // User keys in insertion order.
  std::vector<std::string> keys;
  // Seek targets of present and absent rows, as internal keys.
  std::vector<std::string> hits;
  std::vector<std::string> misses;
  // Erases each key this many inserts after it, as removing applied intents does. 0 to keep all.
  size_t erase_lag = 0;
};

uint16_t HashOf(int64_t pk) {
  return static_cast<uint16_t>(((static_cast<uint64_t>(pk) * 0x9E3779B97F4A7C15ULL) >> 32) %
                               kTabletHashEnd);
}

dockv::DocKey HashDocKey(int64_t pk) {
  return dockv::DocKey(HashOf(pk), dockv::KeyEntryValues{dockv::KeyEntryValue::Int64(pk)});
}

dockv::DocKey RangeDocKey(int64_t pk) {
  return dockv::DocKey(dockv::KeyEntryValues{dockv::KeyEntryValue::Int64(pk)});
}

std::string SeekTarget(const dockv::DocKey& doc_key) {
  return rocksdb::InternalKey(
      doc_key.Encode().AsSlice(), rocksdb::kMaxSequenceNumber,
      rocksdb::kValueTypeForSeek).Encode().ToBuffer();
}

std::vector<int64_t> RandomPks(size_t count, std::mt19937_64* rng) {
  std::unordered_set<int64_t> seen;
  std::vector<int64_t> result;
  while (result.size() != count) {
    auto pk = RandomUniformInt<int64_t>(0, std::numeric_limits<int64_t>::max(), rng);
    if (seen.insert(pk).second) {
      result.push_back(pk);
    }
  }
  return result;
}

class HybridTimeSource {
 public:
  HybridTime Next() {
    return HybridTime::FromMicrosecondsAndLogicalValue(micros_++, 0);
  }

 private:
  MicrosTime micros_ = 1800000000000000;
};

template <class MakeDocKey>
void AddLookups(
    const std::vector<int64_t>& present, const std::vector<int64_t>& absent,
    const MakeDocKey& make_doc_key, std::mt19937_64* rng, Workload* workload) {
  for (size_t i = 0; i != FLAGS_memtable_perf_lookups; ++i) {
    workload->hits.push_back(SeekTarget(make_doc_key(
        present[RandomUniformInt<size_t>(0, present.size() - 1, rng)])));
    workload->misses.push_back(SeekTarget(make_doc_key(
        absent[RandomUniformInt<size_t>(0, absent.size() - 1, rng)])));
  }
}

// Packed rows of a hash partitioned table, inserted in random key order.
Workload HashPacked(std::mt19937_64* rng) {
  Workload result;
  result.name = "hash_packed";
  auto pks = RandomPks(FLAGS_memtable_perf_entries * 2, rng);
  std::vector<int64_t> present(pks.begin(), pks.begin() + FLAGS_memtable_perf_entries);
  std::vector<int64_t> absent(pks.begin() + FLAGS_memtable_perf_entries, pks.end());
  HybridTimeSource hts;
  for (auto pk : present) {
    result.keys.push_back(
        dockv::SubDocKey(HashDocKey(pk), DocHybridTime(hts.Next(), 0)).Encode().ToStringBuffer());
  }
  AddLookups(present, absent, HashDocKey, rng, &result);
  return result;
}

// Packed rows of a range partitioned table, inserted in ascending key order.
Workload RangeSequential(std::mt19937_64* rng) {
  Workload result;
  result.name = "range_sequential";
  std::vector<int64_t> present;
  std::vector<int64_t> absent;
  HybridTimeSource hts;
  for (size_t i = 0; i != FLAGS_memtable_perf_entries; ++i) {
    auto pk = static_cast<int64_t>(i * 2);
    present.push_back(pk);
    absent.push_back(pk + 1);
    result.keys.push_back(
        dockv::SubDocKey(RangeDocKey(pk), DocHybridTime(hts.Next(), 0)).Encode().ToStringBuffer());
  }
  AddLookups(present, absent, RangeDocKey, rng, &result);
  return result;
}

// Rows of five column entries each, a row's columns written together.
Workload HashColumns(std::mt19937_64* rng) {
  constexpr int kColumns = 5;
  Workload result;
  result.name = "hash_columns";
  auto rows = FLAGS_memtable_perf_entries / kColumns;
  auto pks = RandomPks(rows * 2, rng);
  std::vector<int64_t> present(pks.begin(), pks.begin() + rows);
  std::vector<int64_t> absent(pks.begin() + rows, pks.end());
  HybridTimeSource hts;
  for (auto pk : present) {
    auto doc_key = HashDocKey(pk);
    auto ht = hts.Next();
    for (int column = 0; column != kColumns; ++column) {
      result.keys.push_back(dockv::SubDocKey(
          doc_key, DocHybridTime(ht, column),
          dockv::KeyEntryValues{dockv::KeyEntryValue::MakeColumnId(ColumnId(column + 1))})
              .Encode().ToStringBuffer());
    }
  }
  AddLookups(present, absent, HashDocKey, rng, &result);
  return result;
}

// Packed rows each updated four times, the updates in random order.
Workload HashUpdates(std::mt19937_64* rng) {
  constexpr size_t kVersions = 4;
  Workload result;
  result.name = "hash_updates";
  auto rows = FLAGS_memtable_perf_entries / kVersions;
  auto pks = RandomPks(rows * 2, rng);
  std::vector<int64_t> present(pks.begin(), pks.begin() + rows);
  std::vector<int64_t> absent(pks.begin() + rows, pks.end());
  std::vector<int64_t> writes;
  for (size_t i = 0; i != kVersions; ++i) {
    writes.insert(writes.end(), present.begin(), present.end());
  }
  std::shuffle(writes.begin(), writes.end(), *rng);
  HybridTimeSource hts;
  for (auto pk : writes) {
    result.keys.push_back(
        dockv::SubDocKey(HashDocKey(pk), DocHybridTime(hts.Next(), 0)).Encode().ToStringBuffer());
  }
  AddLookups(present, absent, HashDocKey, rng, &result);
  return result;
}

// Intents DB: a strong intent and its reverse index entry per row, 16 rows a transaction.
Workload Intents(std::mt19937_64* rng, bool ascending = false) {
  Workload result;
  result.name = "intents";
  auto rows = FLAGS_memtable_perf_entries / 2;
  std::vector<int64_t> present;
  std::vector<int64_t> absent;
  if (ascending) {
    for (size_t i = 0; i != rows; ++i) {
      present.push_back(static_cast<int64_t>(i * 2));
      absent.push_back(static_cast<int64_t>(i * 2 + 1));
    }
  } else {
    auto pks = RandomPks(rows * 2, rng);
    present.assign(pks.begin(), pks.begin() + rows);
    absent.assign(pks.begin() + rows, pks.end());
  }
  auto make_doc_key = ascending ? &RangeDocKey : &HashDocKey;
  auto intent_types = dockv::GetIntentTypesForWrite(IsolationLevel::SNAPSHOT_ISOLATION);
  char intent_type[2] = {
      dockv::KeyEntryTypeAsChar::kIntentTypeSet, static_cast<char>(intent_types.ToUIntPtr())};
  HybridTimeSource hts;
  TransactionId transaction_id = TransactionId::Nil();
  HybridTime ht;
  IntraTxnWriteId write_id = 0;
  DocHybridTimeBuffer doc_ht_buffer;
  dockv::DocHybridTimeWordBuffer inverted_buffer;
  for (size_t i = 0; i != rows; ++i) {
    if (i % kRowsPerTransaction == 0) {
      transaction_id = TransactionId::GenerateRandom(rng);
      ht = hts.Next();
      write_id = 0;
    }
    auto doc_ht = doc_ht_buffer.EncodeWithValueType(ht, write_id++);
    auto key = make_doc_key(present[i]).Encode().ToStringBuffer();
    key.append(intent_type, sizeof(intent_type));
    key.append(doc_ht.cdata(), doc_ht.size());
    result.keys.push_back(std::move(key));

    std::string reverse_key(1, dockv::KeyEntryTypeAsChar::kTransactionId);
    reverse_key.append(transaction_id.AsSlice().cdata(), transaction_id.size());
    auto inverted = dockv::InvertEncodedDocHT(doc_ht, &inverted_buffer);
    reverse_key.append(inverted.cdata(), inverted.size());
    result.keys.push_back(std::move(reverse_key));
  }
  AddLookups(present, absent, make_doc_key, rng, &result);
  return result;
}

// Intents of applied transactions are erased from the memtable soon after they are written.
Workload IntentsApplied(std::mt19937_64* rng) {
  auto result = Intents(rng);
  result.name = "intents_applied";
  result.erase_lag = 64 * kRowsPerTransaction * 2;
  return result;
}

// The same for a range partitioned table written in ascending key order.
Workload RangeIntentsApplied(std::mt19937_64* rng) {
  auto result = Intents(rng, /* ascending= */ true);
  result.name = "range_intents_applied";
  result.erase_lag = 64 * kRowsPerTransaction * 2;
  return result;
}

using Clock = std::chrono::steady_clock;

double NanosPer(Clock::duration duration, size_t count) {
  return std::chrono::duration<double, std::nano>(duration).count() / std::max<size_t>(count, 1);
}

struct Results {
  double insert_ns;
  size_t memory;
  double hit_ns;
  double miss_ns;
  double scan_ns;
  double full_scan_ns;
  // Readers seek random inserted keys, or the newest one.
  double concurrent_insert_ns;
  double concurrent_seek_ns;
  double hot_insert_ns;
  double hot_seek_ns;
  // Folded entries the reads returned, to check that representations agree.
  uint64_t checksum;
};

class MemTableHolder {
 public:
  explicit MemTableHolder(std::shared_ptr<rocksdb::MemTableRepFactory> factory)
      : options_(MakeOptions(std::move(factory))),
        ioptions_(options_),
        write_buffer_(options_.db_write_buffer_size),
        mem_(new rocksdb::MemTable(
            icmp_, ioptions_, rocksdb::MutableCFOptions(options_, ioptions_), &write_buffer_,
            rocksdb::kMaxSequenceNumber)) {
    mem_->Ref();
  }

  ~MemTableHolder() {
    delete mem_->Unref();
  }

  rocksdb::MemTable* operator->() const {
    return mem_;
  }

 private:
  static rocksdb::Options MakeOptions(std::shared_ptr<rocksdb::MemTableRepFactory> factory) {
    rocksdb::Options result;
    result.memtable_factory = std::move(factory);
    return result;
  }

  rocksdb::InternalKeyComparator icmp_{rocksdb::BytewiseComparator()};
  rocksdb::Options options_;
  rocksdb::ImmutableCFOptions ioptions_;
  rocksdb::WriteBuffer write_buffer_;
  rocksdb::MemTable* mem_;
};

class IteratorHolder {
 public:
  explicit IteratorHolder(const MemTableHolder& mem)
      : iter_(mem->NewIterator(rocksdb::ReadOptions(), &arena_)) {}

  ~IteratorHolder() {
    iter_->~InternalIterator();
  }

  rocksdb::InternalIterator* get() const {
    return iter_;
  }

  rocksdb::InternalIterator* operator->() const {
    return iter_;
  }

 private:
  rocksdb::Arena arena_;
  rocksdb::InternalIterator* iter_;
};

uint64_t Fold(uint64_t checksum, Slice key) {
  return checksum * 31 + key.size() + (key.empty() ? 0 : static_cast<uint8_t>(key.end()[-1]));
}

void InsertAll(const Workload& workload, MemTableHolder* mem, std::atomic<size_t>* published) {
  std::string value(kValueSize, 'v');
  Slice value_slice(value);
  rocksdb::SequenceNumber seq = 0;
  for (size_t i = 0; i != workload.keys.size(); ++i) {
    Slice key_slice(workload.keys[i]);
    (*mem)->Add(++seq, rocksdb::kTypeValue, rocksdb::SliceParts(&key_slice, 1),
                rocksdb::SliceParts(&value_slice, 1));
    if (workload.erase_lag && i >= workload.erase_lag) {
      CHECK((*mem)->Erase(workload.keys[i - workload.erase_lag]));
    }
    if (published) {
      published->store(seq, std::memory_order_release);
    }
  }
}

// Seeks each target, reading the entry found and the one after it.
uint64_t SeekAll(
    rocksdb::InternalIterator* iter, const std::vector<std::string>& targets, size_t next_steps,
    uint64_t checksum) {
  for (const auto& target : targets) {
    const auto* entry = &iter->Seek(target);
    for (size_t i = 0; *entry && i != next_steps; ++i) {
      checksum = Fold(checksum, entry->key);
      entry = &iter->Next();
    }
  }
  return checksum;
}

// Inserts all keys while readers seek inserted keys. Returns ns per insert and per seek.
std::pair<double, double> InsertWithReaders(
    const Workload& workload, const std::vector<std::string>& targets,
    std::shared_ptr<rocksdb::MemTableRepFactory> factory, bool newest) {
  MemTableHolder mem(factory);
  std::atomic<size_t> published{0};
  std::atomic<size_t> seeks{0};
  std::atomic<uint64_t> seek_nanos{0};
  TestThreadHolder thread_holder;
  for (size_t i = 0; i != FLAGS_memtable_perf_readers; ++i) {
    thread_holder.AddThreadFunctor([&, i] {
      IteratorHolder iter(mem);
      std::mt19937_64 rng(i);
      size_t count = 0;
      auto start = Clock::now();
      for (; !thread_holder.stop_flag().load(std::memory_order_acquire); ++count) {
        auto inserted = published.load(std::memory_order_acquire);
        if (inserted == 0) {
          continue;
        }
        auto index = newest ? inserted - 1 : RandomUniformInt<size_t>(0, inserted - 1, &rng);
        auto& entry = iter->Seek(targets[index]);
        CHECK(entry);
        iter->Next();
      }
      seek_nanos.fetch_add(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
      seeks.fetch_add(count);
    });
  }
  auto start = Clock::now();
  InsertAll(workload, &mem, &published);
  auto insert_ns = NanosPer(Clock::now() - start, workload.keys.size());
  thread_holder.Stop();
  return {insert_ns, static_cast<double>(seek_nanos.load()) / std::max<size_t>(seeks, 1)};
}

Results RunWorkload(
    const Workload& workload, std::shared_ptr<rocksdb::MemTableRepFactory> factory) {
  Results results{};
  {
    MemTableHolder mem(factory);
    auto start = Clock::now();
    InsertAll(workload, &mem, nullptr);
    results.insert_ns = NanosPer(Clock::now() - start, workload.keys.size());
    results.memory = mem->ApproximateMemoryUsage();

    IteratorHolder iter(mem);

    start = Clock::now();
    results.checksum = SeekAll(iter.get(), workload.hits, 2, results.checksum);
    results.hit_ns = NanosPer(Clock::now() - start, workload.hits.size());

    start = Clock::now();
    results.checksum = SeekAll(iter.get(), workload.misses, 1, results.checksum);
    results.miss_ns = NanosPer(Clock::now() - start, workload.misses.size());

    std::vector<std::string> scan_targets(
        workload.hits.begin(), workload.hits.begin() + workload.hits.size() / kScanLength);
    start = Clock::now();
    results.checksum = SeekAll(iter.get(), scan_targets, kScanLength, results.checksum);
    results.scan_ns = NanosPer(Clock::now() - start, scan_targets.size());

    start = Clock::now();
    size_t entries = 0;
    for (const auto* entry = &iter->SeekToFirst(); *entry; entry = &iter->Next()) {
      ++entries;
    }
    results.full_scan_ns = NanosPer(Clock::now() - start, entries);
    CHECK_EQ(entries, workload.keys.size() - (
        workload.erase_lag ? workload.keys.size() - workload.erase_lag : 0));
  }
  if (workload.erase_lag) {
    return results;
  }

  std::vector<std::string> targets;
  targets.reserve(workload.keys.size());
  for (const auto& key : workload.keys) {
    targets.push_back(rocksdb::InternalKey(
        key, rocksdb::kMaxSequenceNumber, rocksdb::kValueTypeForSeek).Encode().ToBuffer());
  }
  std::tie(results.concurrent_insert_ns, results.concurrent_seek_ns) =
      InsertWithReaders(workload, targets, factory, /* newest= */ false);
  std::tie(results.hot_insert_ns, results.hot_seek_ns) =
      InsertWithReaders(workload, targets, factory, /* newest= */ true);
  return results;
}

} // namespace

class MemTableRepPerfTest : public YBTest {};

TEST_F(MemTableRepPerfTest, Compare) {
  std::mt19937_64 rng(42);
  std::vector<std::pair<std::string, Workload(*)(std::mt19937_64*)>> generators = {
    {"hash_packed", &HashPacked},
    {"range_sequential", &RangeSequential},
    {"hash_columns", &HashColumns},
    {"hash_updates", &HashUpdates},
    {"intents", [](std::mt19937_64* rng) { return Intents(rng); }},
    {"intents_applied", &IntentsApplied},
    {"range_intents_applied", &RangeIntentsApplied},
  };
  auto selected = StringSplit(FLAGS_memtable_perf_workloads, ',');
  std::vector<std::pair<std::string, std::shared_ptr<rocksdb::MemTableRepFactory>>> factories = {
    {"skiplist",
     std::make_shared<rocksdb::SkipListFactory>(0, rocksdb::ConcurrentWrites::kFalse)},
    {"btree", std::make_shared<rocksdb::BTreeFactory>()},
  };

  std::string report = Format(
      "\n$0 | $1 | $2 | $3 | $4 | $5 | $6 | $7 | $8 | $9 | $10 | $11\n", "workload", "rep",
      "insert ns", "MB", "hit ns", "miss ns", "scan100 ns", "full scan ns/entry",
      "insert, random readers ns", "random reader seek ns", "insert, newest readers ns",
      "newest reader seek ns");
  for (const auto& [name, generator] : generators) {
    if (!FLAGS_memtable_perf_workloads.empty() &&
        std::find(selected.begin(), selected.end(), name) == selected.end()) {
      continue;
    }
    auto workload = generator(&rng);
    std::optional<uint64_t> checksum;
    for (const auto& [rep_name, factory] : factories) {
      auto results = RunWorkload(workload, factory);
      if (checksum) {
        ASSERT_EQ(*checksum, results.checksum) << name << ", " << rep_name;
      }
      checksum = results.checksum;
      report += Format(
          "$0 | $1 | $2 | $3 | $4 | $5 | $6 | $7 | $8 | $9 | $10 | $11\n", name, rep_name,
          FixedPoint(results.insert_ns, 0), results.memory / 1_MB,
          FixedPoint(results.hit_ns, 0), FixedPoint(results.miss_ns, 0),
          FixedPoint(results.scan_ns, 0), FixedPoint(results.full_scan_ns, 1),
          FixedPoint(results.concurrent_insert_ns, 0),
          FixedPoint(results.concurrent_seek_ns, 0),
          FixedPoint(results.hot_insert_ns, 0), FixedPoint(results.hot_seek_ns, 0));
      LOG(INFO) << report;
    }
  }
}

} // namespace yb::docdb
