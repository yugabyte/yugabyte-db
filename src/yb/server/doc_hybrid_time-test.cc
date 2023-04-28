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

#include <string>

#include "yb/common/doc_hybrid_time.h"

#include "yb/gutil/ref_counted.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/string_trim.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;
using std::vector;
using std::cout;
using std::endl;

using yb::server::HybridClock;
using yb::util::sgn;
using strings::Substitute;

namespace yb {

TEST(DocHybridTimeTest, TestDocDbFormatEncodeDecode) {
  auto clock = make_scoped_refptr<HybridClock>(new HybridClock());
  ASSERT_OK(clock->Init());
  vector<DocHybridTime> timestamps;
  vector<string> encoded_timestamps;
  unsigned int seed = SeedRandom();

  // Increase this to e.g. an extreme value of 100 to test how big timestamps will become in the
  // future.
  constexpr int kAddNumYears = 0;

  for (int i = 0; i < 10000; ++i) {
    const IntraTxnWriteId write_id = rand_r(&seed) % 1000 + 1;

    timestamps.emplace_back(
        clock->Now().AddSeconds(kAddNumYears * 3600 * 24 * 365),
        write_id);
    timestamps.pop_back();
    timestamps.push_back(DocHybridTime(1492469267789800, 0, 1));

    const DocHybridTime& last_ts = timestamps.back();
    encoded_timestamps.emplace_back(timestamps.back().EncodedInDocDbFormat());
    const string& last_ts_encoded = encoded_timestamps.back();
    const auto encoded_size = encoded_timestamps.back().size();
    // Encoded size will sometimes be only 6 bytes if both logical component and write id are zero.
    ASSERT_GE(encoded_size, 6);
    ASSERT_LE(encoded_size, 12);
    // We store the encoded length of the whole DocHybridTime into its last 5 bits.
    ASSERT_EQ(encoded_size, last_ts_encoded.back() & 0x1f);
    SCOPED_TRACE(Format("last_ts_encoded: $0", Slice(last_ts_encoded).ToDebugHexString()));
    DocHybridTime decoded_ts = ASSERT_RESULT(DocHybridTime::FullyDecodeFrom(last_ts_encoded));
    ASSERT_EQ(last_ts, decoded_ts);
  }

  for (int iteration = 0; iteration < 10000; ++iteration) {
    const int i = rand_r(&seed) % timestamps.size();
    const int j = rand_r(&seed) % timestamps.size();
    const auto& ts1 = timestamps[i];
    const auto& ts2 = timestamps[j];
    const auto& encoded1 = encoded_timestamps[i];
    const auto& encoded2 = encoded_timestamps[j];
    // Encoded representations of timestamps should compare in the reverse order of decoded ones.
    ASSERT_EQ(sgn(ts1.CompareTo(ts2)), -sgn(encoded1.compare(encoded2)));
  }
}

TEST(DocHybridTimeTest, TestToString) {
  ASSERT_EQ("HT{ physical: 100200300400 }",
            DocHybridTime(100200300400, 0, kMinWriteId).ToString());
  ASSERT_EQ("HT{ physical: 100200300400 logical: 4095 }",
            DocHybridTime(100200300400, 4095, 0).ToString());
  ASSERT_EQ("HT{ physical: 100200300400 w: 123 }",
            DocHybridTime(100200300400, 0, 123).ToString());
  ASSERT_EQ("HT{ physical: 100200300400 logical: 2222 w: 123 }",
            DocHybridTime(100200300400, 2222, 123).ToString());
}

TEST(DocHybridTimeTest, TestExactByteRepresentation) {
  const auto kYugaEpoch = kYugaByteMicrosecondEpoch;

  struct TestDesc {
    string expected_bytes_str;
    MicrosTime micros;
    LogicalTimeComponent logical;
    IntraTxnWriteId write_id;

    DocHybridTime ToHybridTime() const {
      return DocHybridTime (micros, logical, write_id);
    }

    string ActualFormattedByteStr() const {
      return FormatBytesAsStr(ToHybridTime().EncodedInDocDbFormat());
    }
  };
  using std::get;

  vector<TestDesc> test_descriptions{
      TestDesc{ R"#("\x80\x07\xc4e5\xff\x80H")#",
                kYugaEpoch + 1000000000, 0, kMinWriteId },

      TestDesc{ R"#("\x80\x10\xbd\xbf;-\x03\xdf\xff\xff\xff\xec")#",
                kYugaEpoch + 1000000, 1234, 4294967295 },

      TestDesc{ R"#("\x80\x10\xbd\xbf;-G")#",
                kYugaEpoch + 1000000, 1234, kMinWriteId },

      TestDesc{ R"#("\x80\x10\xbd\xbf\x80\x03\xdf\xff\xff\xff\xeb")#",
                kYugaEpoch + 1000000, 0, 4294967295 },

      TestDesc{ R"#("\x80\x10\xbd\xbf\x80F")#",
                kYugaEpoch + 1000000, 0, kMinWriteId },

      TestDesc{ R"#("\x80<\x17\x80E")#",
                kYugaEpoch + 1000, 0, kMinWriteId },

      TestDesc{ R"#("\x80?\x0b=\xbfF")#",
                kYugaEpoch, 1000000, kMinWriteId },

      TestDesc{ R"#("\x80\x80<\x17E")#",
                kYugaEpoch, 1000, kMinWriteId },

      TestDesc{ R"#("\x80\x80\x80\x0e\x17\xb7\xc7")#",
                kYugaEpoch, 0, 1000000 },

      TestDesc{ R"#("\x80\x80\x80\x1f\x82\xc6")#",
                kYugaEpoch, 0, 1000 },

      TestDesc{ R"#("\x80\x80\x80D")#",
                kYugaEpoch, 0, kMinWriteId },

      TestDesc{ R"#("\x80\xc3\xe8\x80E")#",
                kYugaEpoch - 1000, 0, kMinWriteId },

      TestDesc{ R"#("\x80\xefB@\x80F")#",
                kYugaEpoch - 1000000, 0, kMinWriteId },

      TestDesc{ R"#("\x80\xf8;\x9a\xca\x00\x80H")#",
                kYugaEpoch - 1000000000, 0, kMinWriteId },

      TestDesc{ R"#("\x80\xff\x01\xc6\xbfRc@\x00\x80K")#",
                1000000000000000LL, 0, kMinWriteId },

      TestDesc{ R"#("\x80\xff\x05T=\xf7)\xc0\x00\x80K")#",
                kYugaEpoch - 1500000000000000, 0, kMinWriteId },
  };

  // Sort so that the copy-and-paste-able correct answers always come out in the sorted order.
  sort(test_descriptions.begin(), test_descriptions.end(),
       [](const TestDesc& a, const TestDesc& b) -> bool {
         // Sort in reverse order (latest timestamps first).
         return a.ToHybridTime() > b.ToHybridTime();
       });

  // Generate expected answers that can be copied and pasted into the code above.
  for (const auto& t : test_descriptions) {
    string micros_str;
    const auto micros = t.micros;
    if (llabs(static_cast<int64_t>(micros) - static_cast<int64_t>(kYugaEpoch)) <= 1000000000) {
      if (micros == kYugaEpoch) {
        micros_str = "kYugaEpoch";
      } else if (micros > kYugaEpoch) {
        micros_str = Substitute("kYugaEpoch + $0", micros - kYugaByteMicrosecondEpoch);
      } else {
        micros_str = Substitute("kYugaEpoch - $0", kYugaByteMicrosecondEpoch - micros);
      }
    } else {
      micros_str = std::to_string(micros) + "LL";
    }
    cout << Substitute("TestDesc{ R\"#($0)#\",\n"
                       "          $1, $2, $3 },\n",
                       t.ActualFormattedByteStr(), micros_str, t.logical,
                       t.write_id == kMinWriteId ? "kMinWriteId" : std::to_string(t.write_id))
         << endl;
  }

  for (const auto& t : test_descriptions) {
    SCOPED_TRACE(t.ToHybridTime().ToString());
    EXPECT_STR_EQ_VERBOSE_TRIMMED(t.expected_bytes_str, t.ActualFormattedByteStr());
  }
}

TEST(DocHybridTimeTest, DefaultConstructionAndComparison) {
  const auto default_value = DocHybridTime();
  EXPECT_EQ(kInvalidHybridTimeValue, default_value.hybrid_time().value());
  EXPECT_EQ(kMinWriteId, default_value.write_id());

  ASSERT_EQ(DocHybridTime(0, 0, 0), DocHybridTime(0, 0, 0));

  EXPECT_LE(DocHybridTime(0, 0, 0), DocHybridTime(0, 0, 0));
  EXPECT_GE(DocHybridTime(0, 0, 0), DocHybridTime(0, 0, 0));

  EXPECT_LT(DocHybridTime(0, 0, 0), DocHybridTime(1, 0, 0));
  EXPECT_LT(DocHybridTime(0, 0, 0), DocHybridTime(0, 1, 0));
  EXPECT_LT(DocHybridTime(0, 0, 0), DocHybridTime(0, 0, 1));
  EXPECT_LE(DocHybridTime(0, 0, 0), DocHybridTime(1, 0, 0));
  EXPECT_LE(DocHybridTime(0, 0, 0), DocHybridTime(0, 1, 0));
  EXPECT_LE(DocHybridTime(0, 0, 0), DocHybridTime(0, 0, 1));

  EXPECT_GT(DocHybridTime(0, 0, 1), DocHybridTime(0, 0, 0));
  EXPECT_GT(DocHybridTime(0, 1, 0), DocHybridTime(0, 0, 0));
  EXPECT_GT(DocHybridTime(1, 0, 0), DocHybridTime(0, 0, 0));
  EXPECT_GE(DocHybridTime(0, 0, 1), DocHybridTime(0, 0, 0));
  EXPECT_GE(DocHybridTime(0, 1, 0), DocHybridTime(0, 0, 0));
  EXPECT_GE(DocHybridTime(1, 0, 0), DocHybridTime(0, 0, 0));

  EXPECT_NE(DocHybridTime(0, 0, 0), DocHybridTime(1, 0, 0));
  EXPECT_NE(DocHybridTime(0, 0, 0), DocHybridTime(0, 1, 0));
  EXPECT_NE(DocHybridTime(0, 0, 0), DocHybridTime(0, 0, 1));

  EXPECT_LT(DocHybridTime(10, 20, 0), DocHybridTime(20, 10, 0));
  EXPECT_LT(DocHybridTime(0, 10, 20), DocHybridTime(0, 20, 10));
  EXPECT_LT(DocHybridTime(10, 0, 20), DocHybridTime(20, 0, 10));
  EXPECT_LE(DocHybridTime(10, 20, 0), DocHybridTime(20, 10, 0));
  EXPECT_LE(DocHybridTime(0, 10, 20), DocHybridTime(0, 20, 10));
  EXPECT_LE(DocHybridTime(10, 0, 20), DocHybridTime(20, 0, 10));

  EXPECT_GT(DocHybridTime(20, 10, 0), DocHybridTime(10, 20, 0));
  EXPECT_GT(DocHybridTime(0, 20, 10), DocHybridTime(0, 10, 20));
  EXPECT_GT(DocHybridTime(20, 0, 10), DocHybridTime(10, 0, 20));
  EXPECT_GE(DocHybridTime(20, 10, 0), DocHybridTime(10, 20, 0));
  EXPECT_GE(DocHybridTime(0, 20, 10), DocHybridTime(0, 10, 20));
  EXPECT_GE(DocHybridTime(20, 0, 10), DocHybridTime(10, 0, 20));
}

}  // namespace yb
