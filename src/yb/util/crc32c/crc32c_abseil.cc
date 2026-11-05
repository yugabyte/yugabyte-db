// Copyright 2022 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

// Hardware accelerated CRC32C computation on Intel and ARM architecture. Copied from
// absl/crc/internal/crc_x86_arm_combined.cc, crc32_x86_arm_combined_simd.h and crc32c_inline.h
// (Abseil master 921be849a0 of 2026-11-05), because the prebuilt Abseil library is older and
// compiled without the CPU features this code needs. Here the code is compiled for them with target
// attributes, so callers check that the CPU has them first. The code lives in the yb namespace, so
// it does not clash with the Abseil symbols, and Extend also handles the short inputs that
// absl::ExtendCrc32c() handles before calling it. Unknown x86 CPUs with VPCLMULQDQ get the Ice Lake
// tuning instead of the one for CPUs with slow PCLMULQDQ, and CPU models whose tuning needs
// features the CPU does not report get the tuning of a similar model, see AdjustForMissingFeatures.
// NewAbseilEnginesForTests is added at the end.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/endian.h"
#include "absl/base/optimization.h"
#include "absl/base/prefetch.h"
#include "absl/crc/internal/crc_internal.h"
#include "absl/numeric/bits.h"

#include "yb/util/crc32c_accelerated.h"
#include "yb/util/crc32c/cpu_detect.h"

// The code is compiled for the CPU features it needs with target attributes, so the availability of
// the instructions is defined by the architecture, not by the compiler flags as in Abseil.
#if defined(__x86_64__)
#include <x86intrin.h>
#define ABSL_CRC_INTERNAL_HAVE_X86_SIMD
#elif defined(__aarch64__) && defined(__LITTLE_ENDIAN__)
#include <arm_acle.h>
#include <arm_neon.h>
#define ABSL_CRC_INTERNAL_HAVE_ARM_SIMD
#if defined(__linux__)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#endif

// We try to force AVX instructions so that we can use them when AVX is
// available at runtime even if the translation unit isn't built with AVX
// support. Clang on Windows has gnu::target but does not make AVX types like
// __m256i available when trying to force specific functions to use AVX
// compiles.
#if ABSL_HAVE_CPP_ATTRIBUTE(gnu::target) && !defined(_MSC_VER) && \
    (defined(__x86_64__) || defined(__i386__))
#define ABSL_INTERNAL_CAN_FORCE_AVX 1
#endif

#if defined(ABSL_INTERNAL_CAN_FORCE_AVX)
#define ABSL_INTERNAL_ATTRIBUTE_AVX [[gnu::target("avx")]]
#else
#define ABSL_INTERNAL_ATTRIBUTE_AVX
#endif

#if defined(ABSL_CRC_INTERNAL_HAVE_ARM_SIMD) || \
    defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD)
#define ABSL_INTERNAL_CAN_USE_SIMD_CRC32C
#endif

namespace yb::crc::internal {

using absl::PrefetchToLocalCache;
using absl::PrefetchToLocalCacheForWrite;
using absl::crc_internal::kPrefetchHorizon;
using absl::crc_internal::kPrefetchHorizonMedium;
using absl::crc_internal::RoundUp;

#if defined(ABSL_INTERNAL_CAN_USE_SIMD_CRC32C)

// The functions up to TryNewCRC32AcceleratedX86ARMCombined are compiled for the CPU features they
// need. ABSL_INTERNAL_ATTRIBUTE_AVX adds AVX to some of them.
#if defined(__clang__) && defined(__x86_64__)
#pragma clang attribute push(__attribute__((target("sse4.2,pclmul"))), apply_to = function)
#elif defined(__clang__)
#pragma clang attribute push(__attribute__((target("crc,aes"))), apply_to = function)
#elif defined(__x86_64__)
#pragma GCC push_options
#pragma GCC target("sse4.2,pclmul")
#else
#pragma GCC push_options
#pragma GCC target("+crc+crypto")
#endif

namespace {

// Test hooks of the Abseil library, which the non-temporal copy reports to.
[[maybe_unused]] void DebugReportNonTemporalStore() {}
[[maybe_unused]] void DebugReportStoreFence() {}
// Constants for switching between algorithms.
// Chosen by comparing speed at different powers of 2.
// Inputs shorter than kSmallCutoff are processed with a simple serial loop of
// CRC32 instructions instead of dispatching to platform-specific SIMD code.
constexpr size_t kSmallCutoff = 256;
// Inputs shorter than kMediumCutoff may be split into multiple chunks and
// processed in parallel (depending on the CPU we're running on) using
// interleaved CRC32 instructions. Inputs larger than this are split into a
// larger number of chunks and processed with interleaved CRC32 instructions and
// PCLMULQDQ streams.
constexpr size_t kMediumCutoff = 1024;

#if defined(ABSL_CRC_INTERNAL_HAVE_ARM_SIMD)
using V128 = uint64x2_t;
#elif defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD)
// Note: Do not use __m128i_u, it is not portable.
// Use V128_LoadU() perform an unaligned load from __m128i*.
using V128 = __m128i;
#else
using V128 = std::array<uint64_t, 2>;
#endif

#if (defined(__AVX__) || defined(ABSL_INTERNAL_CAN_FORCE_AVX)) && \
    defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD)
using V256 = __m256i;
#else
// Placeholder for V256 when AVX is not available.
struct alignas(32) V256 {
  std::array<uint64_t, 4> val;
};
#endif

// Starting with the initial value in |crc|, accumulates a CRC32 value for
// unsigned integers of different sizes.
uint32_t CRC32_u8(uint32_t crc, uint8_t v);

uint32_t CRC32_u16(uint32_t crc, uint16_t v);

uint32_t CRC32_u32(uint32_t crc, uint32_t v);

uint32_t CRC32_u64(uint32_t crc, uint64_t v);

// Loads 128 bits of integer data. |src| must be 16-byte aligned.
V128 V128_Load(const V128* src);

// Load 128 bits of integer data. |src| does not need to be aligned.
V128 V128_LoadU(const V128* src);

// Store 128 bits of integer data. |src| must be 16-byte aligned.
void V128_Store(V128* dst, V128 data);

// Polynomially multiplies the high 64 bits of |l| and |r|.
V128 V128_PMulHi(const V128 l, const V128 r);

// Polynomially multiplies the low 64 bits of |l| and |r|.
V128 V128_PMulLow(const V128 l, const V128 r);

// Polynomially multiplies the low 64 bits of |r| and high 64 bits of |l|.
V128 V128_PMul01(const V128 l, const V128 r);

// Polynomially multiplies the low 64 bits of |l| and high 64 bits of |r|.
V128 V128_PMul10(const V128 l, const V128 r);

// Produces a XOR operation of |l| and |r|.
V128 V128_Xor(const V128 l, const V128 r);

// Produces a 3-way XOR operation of |a|, |b|, and |c|.
// When `kUseEor3` is true, uses the ARMv8.2-A `EOR3` (3-way Exclusive-OR)
// instruction.
template <bool kUseEor3 = false>
V128 V128_Xor3(const V128 a, const V128 b, const V128 c);

// Sets the lower half of a 128 bit register to the given 64-bit value and
// zeroes the upper half.
// dst[63:0] := |r|
// dst[127:64] := |0|
V128 V128_From64WithZeroFill(const uint64_t r);

// Extracts a 32-bit integer from |l|, selected with |imm|.
template <int imm>
int V128_Extract32(const V128 l);

// Extracts a 64-bit integer from |l|, selected with |imm|.
template <int imm>
uint64_t V128_Extract64(const V128 l);

// Extracts the low 64 bits from V128.
int64_t V128_Low64(const V128 l);

// Add packed 64-bit integers in |l| and |r|.
V128 V128_Add64(const V128 l, const V128 r);

// Performs a store fence on architectures that require it (x86) after
// non-temporal stores.
//
// Debug builds track that this function is called after all tracked
// non-temporal store functions, via the weak hooks
// DebugReportNonTemporalStore() and DebugReportStoreFence() in crc.h.
void NonTemporalStoreFence();

#if (defined(__AVX__) || defined(ABSL_INTERNAL_CAN_FORCE_AVX)) && \
    defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD)
[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline V256 V256_LoadU(const void* src);
[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline V256 V256_Broadcast128(const V128* src);
[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline void V256_StoreU(void* dst, V256 data);
[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline void V256_StoreNonTemporal(void* dst,
                                                              V256 data);
#else
template <typename T = V256>
T V256_LoadU(const void* src);

template <typename T = V256>
T V256_Broadcast128(const V128* src);

template <typename T = V256>
void V256_StoreU(void* dst, const T& data);

template <typename T = V256>
void V256_StoreNonTemporal(void* dst, const T& data);
#endif

[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline void V256_LoadPairU(const void* src,
                                                       V256* v0, V256* v1);
[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline void V256_StorePairU(void* dst, V256 v0,
                                                        V256 v1);
[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline void V256_StorePairNonTemporal(void* dst,
                                                                  V256 v0,
                                                                  V256 v1);
[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline void V256_CopyPairU(void* dst,
                                                       const void* src);
[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline void V256_CopyPairNonTemporal(
    void* dst, const void* src);

#if defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD)

[[maybe_unused]] inline uint32_t CRC32_u8(uint32_t crc, uint8_t v) {
  return _mm_crc32_u8(crc, v);
}

[[maybe_unused]] inline uint32_t CRC32_u16(uint32_t crc, uint16_t v) {
  return _mm_crc32_u16(crc, v);
}

[[maybe_unused]] inline uint32_t CRC32_u32(uint32_t crc, uint32_t v) {
  return _mm_crc32_u32(crc, v);
}

[[maybe_unused]] inline uint32_t CRC32_u64(uint32_t crc, uint64_t v) {
  return static_cast<uint32_t>(_mm_crc32_u64(crc, v));
}

[[maybe_unused]] inline V128 V128_Load(const V128* src) { return _mm_load_si128(src); }

[[maybe_unused]] inline V128 V128_LoadU(const V128* src) { return _mm_loadu_si128(src); }

[[maybe_unused]] inline void V128_Store(V128* dst, V128 data) { _mm_store_si128(dst, data); }

[[maybe_unused]] inline V128 V128_PMulHi(const V128 l, const V128 r) {
  return _mm_clmulepi64_si128(l, r, 0x11);
}

[[maybe_unused]] inline V128 V128_PMulLow(const V128 l, const V128 r) {
  return _mm_clmulepi64_si128(l, r, 0x00);
}

[[maybe_unused]] inline V128 V128_PMul01(const V128 l, const V128 r) {
  return _mm_clmulepi64_si128(l, r, 0x01);
}

[[maybe_unused]] inline V128 V128_PMul10(const V128 l, const V128 r) {
  return _mm_clmulepi64_si128(l, r, 0x10);
}

[[maybe_unused]] inline V128 V128_Xor(const V128 l, const V128 r) { return _mm_xor_si128(l, r); }

template <bool kUseEor3>
[[maybe_unused]] inline V128 V128_Xor3(const V128 a, const V128 b, const V128 c) {
  return V128_Xor(V128_Xor(a, b), c);
}

[[maybe_unused]] inline V128 V128_From64WithZeroFill(const uint64_t r) {
  return _mm_set_epi64x(static_cast<int64_t>(0), static_cast<int64_t>(r));
}

template <int imm>
[[maybe_unused]] inline int V128_Extract32(const V128 l) {
  return _mm_extract_epi32(l, imm);
}

template <int imm>
[[maybe_unused]] inline uint64_t V128_Extract64(const V128 l) {
  return static_cast<uint64_t>(_mm_extract_epi64(l, imm));
}

[[maybe_unused]] inline int64_t V128_Low64(const V128 l) { return _mm_cvtsi128_si64(l); }

[[maybe_unused]] inline V128 V128_Add64(const V128 l, const V128 r) {
  return _mm_add_epi64(l, r);
}
#elif defined(ABSL_CRC_INTERNAL_HAVE_ARM_SIMD)

[[maybe_unused]] inline uint32_t CRC32_u8(uint32_t crc, uint8_t v) { return __crc32cb(crc, v); }

[[maybe_unused]] inline uint32_t CRC32_u16(uint32_t crc, uint16_t v) {
  return __crc32ch(crc, v);
}

[[maybe_unused]] inline uint32_t CRC32_u32(uint32_t crc, uint32_t v) {
  return __crc32cw(crc, v);
}

[[maybe_unused]] inline uint32_t CRC32_u64(uint32_t crc, uint64_t v) {
  return __crc32cd(crc, v);
}

[[maybe_unused]] inline V128 V128_Load(const V128* src) {
  return vld1q_u64(reinterpret_cast<const uint64_t*>(src));
}

[[maybe_unused]] inline V128 V128_LoadU(const V128* src) {
  return vld1q_u64(reinterpret_cast<const uint64_t*>(src));
}

[[maybe_unused]] inline void V128_Store(V128* dst, V128 data) {
  vst1q_u64(reinterpret_cast<uint64_t*>(dst), data);
}

// Using inline assembly as clang does not generate the pmull2 instruction and
// performance drops by 15-20%.
// TODO(b/193678732): Investigate why there is a slight performance hit when
// using intrinsics instead of inline assembly.
[[maybe_unused]] inline V128 V128_PMulHi(const V128 l, const V128 r) {
  uint64x2_t res;
  __asm__ __volatile__("pmull2 %0.1q, %1.2d, %2.2d \n\t"
                       : "=w"(res)
                       : "w"(l), "w"(r));
  return res;
}

// TODO(b/193678732): Investigate why the compiler decides to move the constant
// loop multiplicands from GPR to Neon registers every loop iteration.
[[maybe_unused]] inline V128 V128_PMulLow(const V128 l, const V128 r) {
  uint64x2_t res;
  __asm__ __volatile__("pmull %0.1q, %1.1d, %2.1d \n\t"
                       : "=w"(res)
                       : "w"(l), "w"(r));
  return res;
}

[[maybe_unused]] inline V128 V128_PMul01(const V128 l, const V128 r) {
  return reinterpret_cast<V128>(vmull_p64(
      reinterpret_cast<poly64_t>(vget_high_p64(vreinterpretq_p64_u64(l))),
      reinterpret_cast<poly64_t>(vget_low_p64(vreinterpretq_p64_u64(r)))));
}

[[maybe_unused]] inline V128 V128_PMul10(const V128 l, const V128 r) {
  return reinterpret_cast<V128>(vmull_p64(
      reinterpret_cast<poly64_t>(vget_low_p64(vreinterpretq_p64_u64(l))),
      reinterpret_cast<poly64_t>(vget_high_p64(vreinterpretq_p64_u64(r)))));
}

[[maybe_unused]] inline V128 V128_Xor(const V128 l, const V128 r) { return veorq_u64(l, r); }

template <bool kUseEor3>
[[maybe_unused]] inline V128 V128_Xor3(const V128 a, const V128 b, const V128 c) {
#ifndef __ARM_FEATURE_SHA3
  if constexpr (kUseEor3) {
    // If the binary is compiled without SHA3 support, we need inline assembly
    // to use the EOR3 instruction. We want to only use the below inline
    // assembly block when both the CPU supports EOR3 and the binary is built
    // without it, since we remove the sha3 extension support after the eor3
    // instruction.
    uint64x2_t res;
    __asm__ __volatile__(
        ".arch_extension sha3 \n\t"
        "eor3 %0.16b, %1.16b, %2.16b, %3.16b \n\t"
        ".arch_extension nosha3 \n\t"
        : "=w"(res)
        : "w"(a), "w"(b), "w"(c));
    return res;
  }
#endif
  return V128_Xor(V128_Xor(a, b), c);
}

[[maybe_unused]] inline V128 V128_From64WithZeroFill(const uint64_t r) {
  constexpr uint64x2_t kZero = {0, 0};
  return vsetq_lane_u64(r, kZero, 0);
}

template <int imm>
[[maybe_unused]] inline int V128_Extract32(const V128 l) {
  return vgetq_lane_s32(vreinterpretq_s32_u64(l), imm);
}

template <int imm>
[[maybe_unused]] inline uint64_t V128_Extract64(const V128 l) {
  return vgetq_lane_u64(l, imm);
}

[[maybe_unused]] inline int64_t V128_Low64(const V128 l) {
  return vgetq_lane_s64(vreinterpretq_s64_u64(l), 0);
}

[[maybe_unused]] inline V128 V128_Add64(const V128 l, const V128 r) { return vaddq_u64(l, r); }
#endif  // ABSL_CRC_INTERNAL_HAVE_X86_SIMD || ABSL_CRC_INTERNAL_HAVE_ARM_SIMD

[[maybe_unused]] inline void NonTemporalStoreFence() {
#if defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD)
  _mm_sfence();
#endif
  // Despite other architectures not needing an actual fence instruction, we
  // note the fence in debug builds for consistency with x86.
  DebugReportStoreFence();
}

#if (defined(__AVX__) || defined(ABSL_INTERNAL_CAN_FORCE_AVX)) && \
    defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD)
[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline V256 V256_LoadU(const void* src) {
  return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src));
}

[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline V256 V256_Broadcast128(const V128* src) {
  return _mm256_castps_si256(
      _mm256_broadcast_ps(reinterpret_cast<const __m128*>(src)));
}

[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline void V256_StoreU(void* dst, V256 data) {
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst), data);
}

[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline void V256_StoreNonTemporal(void* dst,
                                                              V256 data) {
  _mm256_stream_si256(reinterpret_cast<__m256i*>(dst), data);
  DebugReportNonTemporalStore();
}
#else
template <typename T>
[[maybe_unused]] inline T V256_LoadU(const void* src) {
  T res;
  std::memcpy(&res, src, sizeof(T));
  return res;
}

template <typename T>
[[maybe_unused]] inline T V256_Broadcast128(const V128* src) {
  (void)src;
  return T{};
}

template <typename T>
[[maybe_unused]] inline void V256_StoreU(void* dst, const T& data) {
  std::memcpy(dst, &data, sizeof(T));
}

template <typename T>
[[maybe_unused]] inline void V256_StoreNonTemporal(void* dst, const T& data) {
  // This architecture has no streaming store, so the data goes through the
  // cache and is already strongly ordered. It is still counted, so that the
  // StoreFence() contract is checked identically on every architecture.
  std::memcpy(dst, &data, sizeof(T));
  DebugReportNonTemporalStore();
}
#endif  // (defined(__AVX__) || defined(ABSL_INTERNAL_CAN_FORCE_AVX)) &&
        // defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD)

[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline void V256_LoadPairU(const void* src,
                                                       V256* v0, V256* v1) {
  const char* src_ptr = reinterpret_cast<const char*>(src);
  *v0 = V256_LoadU(src_ptr);
  *v1 = V256_LoadU(src_ptr + sizeof(V256));
}

[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline void V256_StorePairU(void* dst, V256 v0,
                                                        V256 v1) {
  char* dst_ptr = reinterpret_cast<char*>(dst);
  V256_StoreU(dst_ptr, v0);
  V256_StoreU(dst_ptr + sizeof(V256), v1);
}

[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline void V256_StorePairNonTemporal(void* dst,
                                                                  V256 v0,
                                                                  V256 v1) {
  char* dst_ptr = reinterpret_cast<char*>(dst);
  V256_StoreNonTemporal(dst_ptr, v0);
  V256_StoreNonTemporal(dst_ptr + sizeof(V256), v1);
}

[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline void V256_CopyPairU(void* dst,
                                                       const void* src) {
  V256 v0, v1;
  V256_LoadPairU(src, &v0, &v1);
  V256_StorePairU(dst, v0, v1);
}

[[maybe_unused]] ABSL_INTERNAL_ATTRIBUTE_AVX inline void V256_CopyPairNonTemporal(
    void* dst, const void* src) {
  V256 v0, v1;
  V256_LoadPairU(src, &v0, &v1);
  V256_StorePairNonTemporal(dst, v0, v1);
}


// The serial loop of ExtendCrc32cInline() in absl/crc/internal/crc32c_inline.h, which
// absl::ExtendCrc32c() uses for inputs shorter than kSmallCutoff instead of the engine.
void ExtendShort(uint32_t* crc, const char* p, size_t n) {
  if (n & 1) {
    *crc = CRC32_u8(*crc, static_cast<uint8_t>(*p));
    n--;
    p++;
  }
  if (n & 2) {
    *crc = CRC32_u16(*crc, absl::little_endian::Load16(p));
    n -= 2;
    p += 2;
  }
  if (n & 4) {
    *crc = CRC32_u32(*crc, absl::little_endian::Load32(p));
    n -= 4;
    p += 4;
  }
  while (n) {
    *crc = CRC32_u64(*crc, absl::little_endian::Load64(p));
    n -= 8;
    p += 8;
  }
}

}  // namespace

// Implementation details not exported outside of file
namespace {

// Some machines have CRC acceleration hardware.
// We can do a faster version of Extend() on such machines.
class CRC32AcceleratedX86ARMCombined : public Crc32cEngine {
 public:
  CRC32AcceleratedX86ARMCombined() {}
  ~CRC32AcceleratedX86ARMCombined() override {}
  uint32_t ComputeZeroConstant(size_t length) const;

 private:
  CRC32AcceleratedX86ARMCombined(const CRC32AcceleratedX86ARMCombined&) =
      delete;
  CRC32AcceleratedX86ARMCombined& operator=(
      const CRC32AcceleratedX86ARMCombined&) = delete;
};

#define ABSL_INTERNAL_STEP1(crc, p)                   \
  do {                                                \
    crc = CRC32_u8(static_cast<uint32_t>(crc), *p++); \
  } while (0)
#define ABSL_INTERNAL_STEP2(crc, p)                                            \
  do {                                                                         \
    crc =                                                                      \
        CRC32_u16(static_cast<uint32_t>(crc), absl::little_endian::Load16(p)); \
    p += 2;                                                                    \
  } while (0)
#define ABSL_INTERNAL_STEP4(crc, p)                                            \
  do {                                                                         \
    crc =                                                                      \
        CRC32_u32(static_cast<uint32_t>(crc), absl::little_endian::Load32(p)); \
    p += 4;                                                                    \
  } while (0)
#define ABSL_INTERNAL_STEP8(crc, p)                                            \
  do {                                                                         \
    crc =                                                                      \
        CRC32_u64(static_cast<uint32_t>(crc), absl::little_endian::Load64(p)); \
    p += 8;                                                                    \
  } while (0)
#define ABSL_INTERNAL_STEP8BY2(crc0, crc1, p0, p1) \
  do {                                             \
    ABSL_INTERNAL_STEP8(crc0, p0);                 \
    ABSL_INTERNAL_STEP8(crc1, p1);                 \
  } while (0)
#define ABSL_INTERNAL_STEP8BY3(crc0, crc1, crc2, p0, p1, p2) \
  do {                                                       \
    ABSL_INTERNAL_STEP8(crc0, p0);                           \
    ABSL_INTERNAL_STEP8(crc1, p1);                           \
    ABSL_INTERNAL_STEP8(crc2, p2);                           \
  } while (0)

namespace {

// Does polynomial multiplication a * b * x^33 mod G.
//
// One of the multiplicands needs to have an extra factor of x^-33 to cancel out
// the extra factor of x^33. The extra factor of x^33 comes from:
//
// - x^1 from the carry-less multiplication, due to the
//   "least-significant-bit-first" convention of CRC-32C.
//
// - x^32 from using CRC32_u64() to reduce the carry-less product to 32 bits.
//
// Both could be avoided, but at the cost of extra instructions. It's more
// efficient to just drop a factor of x^33 from one of the multiplicands.
uint32_t MultiplyWithExtraX33(uint32_t a, uint32_t b) {
  V128 a_vec = V128_From64WithZeroFill(a);
  V128 b_vec = V128_From64WithZeroFill(b);
  V128 res = V128_PMulLow(a_vec, b_vec);

  return CRC32_u64(0, static_cast<uint64_t>(V128_Low64(res)));
}

// The number of low-order bits that ComputeZeroConstant() drops from the
// length, i.e. treats as zeroes
constexpr int kNumDroppedBits = 4;

// Precomputed constants for faster ExtendByZeroes(). This was generated by
// gen_crc32c_consts.py. The entry at index i is x^(2^(i + 3 + kNumDroppedBits)
// - 33) mod G. That is x^-33 times the polynomial by which the CRC value needs
// to be multiplied to extend it by 2^(i + 3 + kNumDroppedBits) zero bits, or
// equivalently 2^(i + kNumDroppedBits) zero bytes. The extra factor of x^-33
// cancels out the extra factor of x^33 that MultiplyWithExtraX33() introduces.
constexpr uint32_t kCRC32CPowers[] = {
    0x493c7d27, 0xba4fc28e, 0x9e4addf8, 0x0d3b6092, 0xb9e02b86, 0xdd7e3b0c,
    0x170076fa, 0xa51b6135, 0x82f89c77, 0x54a86326, 0x1dc403cc, 0x5ae703ab,
    0xc5013a36, 0xac2ac6dd, 0x9b4615a9, 0x688d1c61, 0xf6af14e6, 0xb6ffe386,
    0xb717425b, 0x478b0d30, 0x54cc62e5, 0x7b2102ee, 0x8a99adef, 0xa7568c8f,
    0xd610d67e, 0x6b086b3f, 0xd94f3c0b, 0xbf818109, 0x780d5a4d, 0x05ec76f1,
    0x00000001, 0x493c7d27, 0xba4fc28e, 0x9e4addf8, 0x0d3b6092, 0xb9e02b86,
    0xdd7e3b0c, 0x170076fa, 0xa51b6135, 0x82f89c77, 0x54a86326, 0x1dc403cc,
    0x5ae703ab, 0xc5013a36, 0xac2ac6dd, 0x9b4615a9, 0x688d1c61, 0xf6af14e6,
    0xb6ffe386, 0xb717425b, 0x478b0d30, 0x54cc62e5, 0x7b2102ee, 0x8a99adef,
    0xa7568c8f, 0xd610d67e, 0x6b086b3f, 0xd94f3c0b, 0xbf818109, 0x780d5a4d,
};
// There must be an entry for each non-dropped bit in the size_t length.
static_assert(std::size(kCRC32CPowers) >= sizeof(size_t) * 8 - kNumDroppedBits);

}  // namespace

// Compute a magic constant, so that multiplying by it is the same as extending
// crc by length zeros. The lowest kNumDroppedBits of the length are ignored and
// treated as zeroes; the caller is assumed to handle any nonzero bits there.
#if defined(NDEBUG) && ABSL_HAVE_CPP_ATTRIBUTE(clang::no_sanitize)
// The array accesses in this are safe: `length >= size_t{1} <<
// kNumDroppedBits`, so `countr_zero(length >> kNumDroppedBits) < sizeof(size_t)
// * 8 - kNumDroppedBits`, and `length & (length - 1)` cannot introduce bits
// `>= sizeof(size_t) * 8 - kNumDroppedBits`. The compiler cannot prove this, so
// manually disable bounds checking.
[[clang::no_sanitize("array-bounds")]]
#endif
uint32_t CRC32AcceleratedX86ARMCombined::ComputeZeroConstant(
    size_t length) const {
  length >>= kNumDroppedBits;

  int index = absl::countr_zero(length);
  uint32_t prev = kCRC32CPowers[index];
  length &= length - 1;

  while (length) {
    // For each bit of length, extend by 2**n zeros.
    index = absl::countr_zero(length);
    prev = MultiplyWithExtraX33(prev, kCRC32CPowers[index]);
    length &= length - 1;
  }
  return prev;
}


// Taken from Intel paper "Fast CRC Computation for iSCSI Polynomial Using CRC32
// Instruction"
// We only need every 4th value, because we unroll loop by 4.
alignas(16) constexpr uint64_t kClmulConstants[] = {
    0x09e4addf8, 0x0ba4fc28e, 0x00d3b6092, 0x09e4addf8, 0x0ab7aff2a,
    0x102f9b8a2, 0x0b9e02b86, 0x00d3b6092, 0x1bf2e8b8a, 0x18266e456,
    0x0d270f1a2, 0x0ab7aff2a, 0x11eef4f8e, 0x083348832, 0x0dd7e3b0c,
    0x0b9e02b86, 0x0271d9844, 0x1b331e26a, 0x06b749fb2, 0x1bf2e8b8a,
    0x0e6fc4e6a, 0x0ce7f39f4, 0x0d7a4825c, 0x0d270f1a2, 0x026f6a60a,
    0x12ed0daac, 0x068bce87a, 0x11eef4f8e, 0x1329d9f7e, 0x0b3e32c28,
    0x0170076fa, 0x0dd7e3b0c, 0x1fae1cc66, 0x010746f3c, 0x086d8e4d2,
    0x0271d9844, 0x0b3af077a, 0x093a5f730, 0x1d88abd4a, 0x06b749fb2,
    0x0c9c8b782, 0x0cec3662e, 0x1ddffc5d4, 0x0e6fc4e6a, 0x168763fa6,
    0x0b0cd4768, 0x19b1afbc4, 0x0d7a4825c, 0x123888b7a, 0x00167d312,
    0x133d7a042, 0x026f6a60a, 0x000bcf5f6, 0x19d34af3a, 0x1af900c24,
    0x068bce87a, 0x06d390dec, 0x16cba8aca, 0x1f16a3418, 0x1329d9f7e,
    0x19fb2a8b0, 0x02178513a, 0x1a0f717c4, 0x0170076fa,
};

enum class PclmulStreamType {
  NONE,
  PCLMUL,
  VPCLMUL,
  NEON_PCLMUL,
  NEON_PCLMUL_EOR3,
};

// Selects the kind of stores used to write the destination buffer when the CRC
// is computed while copying.
enum class CopyType {
  // Don't copy at all, just compute the CRC of the input.
  kNone,
  // Copy with regular stores, leaving the destination in the cache.
  kTemporal,
  // Copy with non-temporal stores, which bypass some levels of cache.
  kNonTemporal,
};

// Base class for CRC32AcceleratedX86ARMCombinedMultipleStreams containing the
// methods and data that don't need the template arguments.
class CRC32AcceleratedX86ARMCombinedMultipleStreamsBase
    : public CRC32AcceleratedX86ARMCombined {
 protected:
  // Copies 64 bytes from `src` to `dst`, using the kind of stores selected by
  // `copy_type`. Does nothing if `copy_type` is NONE.
  template <CopyType copy_type>
  ABSL_INTERNAL_ATTRIBUTE_AVX ABSL_ATTRIBUTE_ALWAYS_INLINE static void
  Copy64Bytes(uint8_t* dst, const uint8_t* src) {
    if constexpr (copy_type == CopyType::kNonTemporal) {
      V256_CopyPairNonTemporal(dst, src);
    } else if constexpr (copy_type == CopyType::kTemporal) {
      V256_CopyPairU(dst, src);
    } else {
      static_assert(copy_type == CopyType::kNone);
    }
  }

  // Stores the 64 bytes held in `v0` and `v1` to `dst`, using the kind of
  // stores selected by `copy_type`. Does nothing if `copy_type` is NONE.
  template <CopyType copy_type>
  ABSL_INTERNAL_ATTRIBUTE_AVX ABSL_ATTRIBUTE_ALWAYS_INLINE static void
  Store64Bytes(uint8_t* dst, V256 v0, V256 v1) {
    if constexpr (copy_type == CopyType::kNonTemporal) {
      V256_StorePairNonTemporal(dst, v0, v1);
    } else if constexpr (copy_type == CopyType::kTemporal) {
      V256_StorePairU(dst, v0, v1);
    } else {
      static_assert(copy_type == CopyType::kNone);
    }
  }

  // Update a single 128-bit element of partialCRC with crc of 16 byte block.
  // Calling FinalizePclmulStream would produce a single crc checksum, but it is
  // expensive. PCLMULQDQ has a high latency, so we run 4 128-bit partial
  // checksums that can be reduced to a single value by FinalizePclmulStream
  // later. Computing crc for arbitrary polynomials with PCLMULQDQ is described
  // in Intel paper "Fast CRC Computation for Generic Polynomials Using
  // PCLMULQDQ Instruction"
  // We are applying it to CRC32C polynomial.
  ABSL_ATTRIBUTE_ALWAYS_INLINE V128 Pclmul16Bytes(V128* partialCRC) const {
    V128 loopMultiplicands =
        V128_Load(reinterpret_cast<const V128*>(kFoldAcross512Bits));
    V128 tmp = V128_PMulHi(*partialCRC, loopMultiplicands);
    *partialCRC = V128_PMulLow(*partialCRC, loopMultiplicands);
    return tmp;
  }

#if defined(ABSL_CRC_INTERNAL_HAVE_ARM_SIMD)
  template <bool kUseEor3 = false>
  ABSL_ATTRIBUTE_ALWAYS_INLINE void Xor16BytesNeonPclmul(
      V128 data, V128 tmp, V128* partialCRC) const {
    *partialCRC = V128_Xor3<kUseEor3>(tmp, *partialCRC, data);
  }

  // Reduce partialCRC produced by Pclmul16Bytes and Xor16BytesNeonPclmul
  // into a single value, that represents crc checksum of all the processed
  // bytes.
  template <bool kUseEor3 = false>
  ABSL_ATTRIBUTE_ALWAYS_INLINE uint64_t
  FinalizeNeonPclmulStream(V128* partialCRC) const {
    V128 partialCRC1 = partialCRC[0];
    V128 partialCRC2 = partialCRC[1];
    V128 partialCRC3 = partialCRC[2];
    V128 partialCRC4 = partialCRC[3];

    // Combine 4 vectors of partial crc into a single vector.
    V128 reductionMultiplicands =
        V128_Load(reinterpret_cast<const V128*>(kFoldAcross256Bits));

    V128 low = V128_PMulLow(reductionMultiplicands, partialCRC1);
    V128 high = V128_PMulHi(reductionMultiplicands, partialCRC1);

    partialCRC1 = V128_Xor3<kUseEor3>(low, high, partialCRC3);

    low = V128_PMulLow(reductionMultiplicands, partialCRC2);
    high = V128_PMulHi(reductionMultiplicands, partialCRC2);

    partialCRC2 = V128_Xor3<kUseEor3>(low, high, partialCRC4);

    reductionMultiplicands =
        V128_Load(reinterpret_cast<const V128*>(kFoldAcross128Bits));

    low = V128_PMulLow(reductionMultiplicands, partialCRC1);
    high = V128_PMulHi(reductionMultiplicands, partialCRC1);
    V128 fullCRC = V128_Xor3<kUseEor3>(low, high, partialCRC2);

    // Reduce fullCRC into scalar value.
    uint32_t crc = 0;
    crc = CRC32_u64(crc, V128_Extract64<0>(fullCRC));
    crc = CRC32_u64(crc, V128_Extract64<1>(fullCRC));
    return crc;
  }

  ABSL_ATTRIBUTE_ALWAYS_INLINE void Xor16BytesPclmul(V128, V128, V128*) const {}

  ABSL_ATTRIBUTE_ALWAYS_INLINE uint64_t FinalizePclmulStream(V128*) const {
    return 0;
  }
#else
  ABSL_ATTRIBUTE_ALWAYS_INLINE void Xor16BytesPclmul(V128 data, V128 tmp,
                                                     V128* partialCRC) const {
    *partialCRC = V128_Xor(V128_Xor(tmp, *partialCRC), data);
  }

  // Reduce partialCRC produced by Pclmul16Bytes and Xor16BytesPclmul into a
  // single value, that represents crc checksum of all the processed bytes.
  ABSL_ATTRIBUTE_ALWAYS_INLINE uint64_t
  FinalizePclmulStream(V128* partialCRC) const {
    V128 partialCRC1 = partialCRC[0];
    V128 partialCRC2 = partialCRC[1];
    V128 partialCRC3 = partialCRC[2];
    V128 partialCRC4 = partialCRC[3];

    // Combine 4 vectors of partial crc into a single vector.
    V128 reductionMultiplicands =
        V128_Load(reinterpret_cast<const V128*>(kFoldAcross256Bits));

    V128 low = V128_PMulLow(reductionMultiplicands, partialCRC1);
    V128 high = V128_PMulHi(reductionMultiplicands, partialCRC1);

    partialCRC1 = V128_Xor(low, high);
    partialCRC1 = V128_Xor(partialCRC1, partialCRC3);

    low = V128_PMulLow(reductionMultiplicands, partialCRC2);
    high = V128_PMulHi(reductionMultiplicands, partialCRC2);

    partialCRC2 = V128_Xor(low, high);
    partialCRC2 = V128_Xor(partialCRC2, partialCRC4);

    reductionMultiplicands =
        V128_Load(reinterpret_cast<const V128*>(kFoldAcross128Bits));

    low = V128_PMulLow(reductionMultiplicands, partialCRC1);
    high = V128_PMulHi(reductionMultiplicands, partialCRC1);
    V128 fullCRC = V128_Xor(low, high);
    fullCRC = V128_Xor(fullCRC, partialCRC2);

    // Reduce fullCRC into scalar value.
    uint32_t crc = 0;
    crc = CRC32_u64(crc, V128_Extract64<0>(fullCRC));
    crc = CRC32_u64(crc, V128_Extract64<1>(fullCRC));
    return crc;
  }

  template <bool kUseEor3 = false>
  ABSL_INTERNAL_ATTRIBUTE_AVX ABSL_ATTRIBUTE_ALWAYS_INLINE void
  Xor16BytesNeonPclmul(V128, V128, V128*) const {}

  template <bool kUseEor3 = false>
  ABSL_INTERNAL_ATTRIBUTE_AVX ABSL_ATTRIBUTE_ALWAYS_INLINE uint64_t
  FinalizeNeonPclmulStream(V128*) const {
    return 0;
  }
#endif

  // Update crc with 8 bytes of data from p.
  ABSL_ATTRIBUTE_ALWAYS_INLINE uint64_t Process8BytesCRC(const uint8_t* p,
                                                         uint64_t crc) const {
    return CRC32_u64(static_cast<uint32_t>(crc),
                     absl::little_endian::Load64(p));
  }

  // Update crc with 16 bytes of data from p.
  ABSL_ATTRIBUTE_ALWAYS_INLINE uint64_t Process16BytesCRC(const uint8_t* p,
                                                          uint64_t crc) const {
    crc = Process8BytesCRC(p, crc);
    crc = Process8BytesCRC(p + 8, crc);
    return crc;
  }

#if defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD) &&                   \
    (defined(__AVX__) || defined(ABSL_INTERNAL_CAN_FORCE_AVX)) && \
    (!defined(_MSC_VER) || defined(__clang__))
  // This is only used if we have vector version of PCLMULQDQ.
  // We don't have it on arm, and it isn't supported by default
  // compiler targets on x86. If we want to use it, we need to either use
  // new compiler flags for the whole function and compile it twice
  // with new and default flags or use inline asm.
  // The code below is the same as FinalizePclmulStream, but with
  // PCLMUL and XOR operating on 2 values in a vector at the same time.
  ABSL_INTERNAL_ATTRIBUTE_AVX ABSL_ATTRIBUTE_ALWAYS_INLINE uint64_t
  FinalizeVpclmulStream(V256* partialCRC) const {
    uint64_t crc = 0;
    uint64_t low64, high64;
    __asm__(
        // reduce 2 256-bit vectors into s single 256 vector
        "vbroadcasti128 %[k256], %%ymm0 \n"
        "vpclmulqdq $0x00, %%ymm0, %[crc0], %%ymm1 \n"
        "vpclmulqdq $0x11, %%ymm0, %[crc0], %%ymm2 \n"
        "vpxor %%ymm2, %%ymm1, %%ymm1 \n"
        "vpxor %[crc1], %%ymm1, %%ymm1 \n"
        // reduce upper and lower parts of 256-bit vector
        "vextracti128 $1, %%ymm1, %%xmm2 \n"
        "vpclmulqdq $0x00, %[k128], %%xmm1, %%xmm3 \n"
        "vpclmulqdq $0x11, %[k128], %%xmm1, %%xmm1 \n"
        "vpxor %%xmm1, %%xmm3, %%xmm3 \n"
        "vpxor %%xmm2, %%xmm3, %%xmm3 \n"
        // Move 2 parts of 128-bit vector into scalar register
        // and reduce using sacalr crc instruction
        "vmovq %%xmm3, %[low] \n"
        "vpextrq $1, %%xmm3, %[high] \n"
        "crc32q %[low], %[crc_out] \n"
        "crc32q %[high], %[crc_out] \n"
        : [crc_out] "+r"(crc), [low] "=&r"(low64), [high] "=&r"(high64)
        : [k256] "m"(*(const __m128i*)kFoldAcross256Bits),
          [crc0] "x"(partialCRC[0]), [crc1] "x"(partialCRC[1]),
          [k128] "m"(*(const __m128i*)kFoldAcross128Bits)
        : "ymm0", "ymm1", "ymm2", "ymm3");
    return crc;
  }

  ABSL_INTERNAL_ATTRIBUTE_AVX ABSL_ATTRIBUTE_ALWAYS_INLINE V256
  Pclmul32BytesVpclmul(V256* partialCRC) const {
    V256 loopMultiplicands =
        V256_Broadcast128(reinterpret_cast<const V128*>(kFoldAcross512Bits));
    V256 tmp;
    __asm__ volatile(
        "vpclmulqdq $0x11, %2, %0, %1 \n"
        "vpclmulqdq $0x00, %2, %0, %0 \n"
        : "+x"(*partialCRC), "=&x"(tmp)
        : "x"(loopMultiplicands));
    return tmp;
  }

  ABSL_INTERNAL_ATTRIBUTE_AVX ABSL_ATTRIBUTE_ALWAYS_INLINE void
  Xor32BytesVpclmul(const uint8_t* p, V256 tmp, V256* partialCRC) const {
    V256 v = V256_LoadU(p);
    __asm__ volatile(
        "vpxor %2, %0, %0 \n"
        "vpxor %1, %0, %0 \n"
        : "+x"(*partialCRC)
        : "x"(v), "x"(tmp));
  }
#else
  template <typename T = V256>
  ABSL_ATTRIBUTE_ALWAYS_INLINE T Pclmul32BytesVpclmul(T*) const {
    static_assert(sizeof(T) == 0, "Vector PCLMUL not supported");
    return T{};
  }
  template <typename T = V256>
  ABSL_ATTRIBUTE_ALWAYS_INLINE void Xor32BytesVpclmul(const uint8_t*, T,
                                                      T*) const {
    static_assert(sizeof(T) == 0, "Vector PCLMUL not supported");
  }
  ABSL_ATTRIBUTE_ALWAYS_INLINE uint64_t FinalizeVpclmulStream(V256*) const {
    return 0;
  }
#endif  // defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD) && (defined(__AVX__) ||
        // defined(ABSL_INTERNAL_CAN_FORCE_AVX)) && (!defined(_MSC_VER) ||
        // defined(__clang__))

  // Constants generated by './scripts/gen-crc-consts.py x86_pclmul
  // crc32_lsb_0x82f63b78' from the Linux kernel.
  alignas(16) static constexpr uint64_t kFoldAcross512Bits[2] = {
      // (x^543 mod G) * x^32
      0x00000000740eef02,
      // (x^479 mod G) * x^32
      0x000000009e4addf8};
  alignas(16) static constexpr uint64_t kFoldAcross256Bits[2] = {
      // (x^287 mod G) * x^32
      0x000000003da6d0cb,
      // (x^223 mod G) * x^32
      0x00000000ba4fc28e};
  alignas(16) static constexpr uint64_t kFoldAcross128Bits[2] = {
      // (x^159 mod G) * x^32
      0x00000000f20c0dfe,
      // (x^95 mod G) * x^32
      0x00000000493c7d27};

  // Medium runs of bytes are broken into groups of kGroupsSmall blocks of same
  // size. Each group is CRCed in parallel then combined at the end of the
  // block.
  static constexpr size_t kGroupsSmall = 3;
  // For large runs we use up to kMaxStreams blocks computed with CRC
  // instruction, and up to kMaxStreams blocks computed with PCLMULQDQ, which
  // are combined in the end.
  static constexpr size_t kMaxStreams = 3;
  // When copying, at most kMaxCopyStreams streams are used in total.
  static constexpr size_t kMaxCopyStreams = 2;
  // On AArch64, full-cache-line stores engage hardware write-streaming, and
  // software destination prefetches force unnecessary DRAM reads.
#if defined(__aarch64__)
  static constexpr bool kShouldPrefetchDst = false;
#else
  static constexpr bool kShouldPrefetchDst = true;
#endif
  static constexpr size_t kDstPrefetchHorizon = 2 * ABSL_CACHELINE_SIZE;
};

template <size_t num_crc_streams, size_t num_pclmul_streams,
          PclmulStreamType pclmul_stream_type>
class CRC32AcceleratedX86ARMCombinedMultipleStreams
    : public CRC32AcceleratedX86ARMCombinedMultipleStreamsBase {
 public:
  void Extend(uint32_t* crc, const void* bytes,
              const size_t length) const override {
    if (length < kSmallCutoff) {
      ExtendShort(crc, static_cast<const char*>(bytes), length);
      return;
    }
    Extend<CopyType::kNone>(crc, bytes, length);
  }

  void ExtendAndCopy(uint32_t* crc, void* __restrict dst,
                     const void* __restrict src, size_t length,
                     bool non_temporal) const {
    if (non_temporal) {
      Extend<CopyType::kNonTemporal>(crc, src, length, dst);
    } else {
      Extend<CopyType::kTemporal>(crc, src, length, dst);
    }
  }

  // Requires `length >= kSmallCutoff`. Shorter inputs are handled by
  // `ExtendCrc32cInline()` in `absl::ExtendCrc32c()` and
  // `CrcMemcpy::CrcAndCopy()`.
  template <CopyType copy_type>
  ABSL_ATTRIBUTE_HOT void Extend(uint32_t* crc, const void* bytes,
                                 const size_t length,
                                 void* __restrict dst = nullptr) const {
    assert(length >= kSmallCutoff);
    if constexpr (pclmul_stream_type != PclmulStreamType::NONE) {
      if (length >= kMediumCutoff) {
        ExtendWithPclmul<copy_type>(crc, bytes, length, dst);
        return;
      }
    }
    ExtendWithoutPclmul<copy_type != CopyType::kNone>(crc, bytes, length, dst);
  }

  template <bool copy>
  ABSL_ATTRIBUTE_HOT void ExtendWithoutPclmul(
      uint32_t* crc, const void* bytes, const size_t length,
      void* __restrict dst = nullptr) const {
    static_assert(num_crc_streams >= 1 && num_crc_streams <= kMaxStreams,
                  "Invalid number of crc streams");
    static_assert(num_pclmul_streams >= 0 && num_pclmul_streams <= kMaxStreams,
                  "Invalid number of pclmul streams");
    const uint8_t* p = static_cast<const uint8_t*>(bytes);
    const uint8_t* e = p + length;
    char* d = static_cast<char*>(dst);
    uint32_t l = *crc;
    uint64_t l64;
    if constexpr (copy) {
      std::memcpy(d, p, length);
    }

    // A single CRC stream just runs the simple loop used to process the
    // remaining bytes after the multi-stream strategy below. Inputs shorter
    // than kSmallCutoff never reach this function (see `Extend()`).
    if constexpr (num_crc_streams > 1) {
      l64 = l;
      const size_t blockSize = 32;
      size_t bs = static_cast<size_t>(e - p) / kGroupsSmall / blockSize;
      uint64_t l641 = 0;
      uint64_t l642 = 0;
      const uint8_t* p1 = p + bs * blockSize;
      const uint8_t* p2 = p1 + bs * blockSize;

      for (size_t i = 0; i + 1 < bs; ++i) {
        ABSL_INTERNAL_STEP8BY3(l64, l641, l642, p, p1, p2);
        ABSL_INTERNAL_STEP8BY3(l64, l641, l642, p, p1, p2);
        ABSL_INTERNAL_STEP8BY3(l64, l641, l642, p, p1, p2);
        ABSL_INTERNAL_STEP8BY3(l64, l641, l642, p, p1, p2);
        PrefetchToLocalCache(
            reinterpret_cast<const char*>(p + kPrefetchHorizonMedium));
        PrefetchToLocalCache(
            reinterpret_cast<const char*>(p1 + kPrefetchHorizonMedium));
        PrefetchToLocalCache(
            reinterpret_cast<const char*>(p2 + kPrefetchHorizonMedium));
      }
      // Don't run crc on last 8 bytes.
      ABSL_INTERNAL_STEP8BY3(l64, l641, l642, p, p1, p2);
      ABSL_INTERNAL_STEP8BY3(l64, l641, l642, p, p1, p2);
      ABSL_INTERNAL_STEP8BY3(l64, l641, l642, p, p1, p2);
      ABSL_INTERNAL_STEP8BY2(l64, l641, p, p1);

      // kClmulConstants stores precomputed constants for up to 32 blocks
      // (bs <= 32).
      constexpr size_t kMaxPrecomputedBlocks = std::size(kClmulConstants) / 2;
      V128 magic;
      if (bs <= kMaxPrecomputedBlocks) {
        magic =
            V128_Load(reinterpret_cast<const V128*>(kClmulConstants) + bs - 1);
      } else {
        uint32_t high = ComputeZeroConstant(32 * bs);
        uint32_t low = MultiplyWithExtraX33(high, high);
        alignas(16) uint64_t dynamic_magic[2] = {low, high};
        magic = V128_Load(reinterpret_cast<const V128*>(dynamic_magic));
      }

      V128 tmp = V128_From64WithZeroFill(l64);

      V128 res1 = V128_PMulLow(tmp, magic);

      tmp = V128_From64WithZeroFill(l641);

      V128 res2 = V128_PMul10(tmp, magic);
      V128 x = V128_Xor(res1, res2);
      l64 = static_cast<uint64_t>(V128_Low64(x)) ^
            absl::little_endian::Load64(p2);
      l64 = CRC32_u64(static_cast<uint32_t>(l642), l64);

      p = p2 + 8;
      l = static_cast<uint32_t>(l64);
    }

    uint64_t remaining_bytes = static_cast<uint64_t>(e - p);
    // Process the remaining bytes.
    while ((e - p) >= 16) {
      ABSL_INTERNAL_STEP8(l, p);
      ABSL_INTERNAL_STEP8(l, p);
    }

    if (remaining_bytes & 8) {
      ABSL_INTERNAL_STEP8(l, p);
    }
    if (remaining_bytes & 4) {
      ABSL_INTERNAL_STEP4(l, p);
    }
    if (remaining_bytes & 2) {
      ABSL_INTERNAL_STEP2(l, p);
    }
    if (remaining_bytes & 1) {
      ABSL_INTERNAL_STEP1(l, p);
    }

    *crc = l;
  }

  template <CopyType copy_type>
  ABSL_INTERNAL_ATTRIBUTE_AVX ABSL_ATTRIBUTE_HOT void ExtendWithPclmul(
      uint32_t* crc, const void* bytes, const size_t length,
      void* __restrict dst = nullptr) const {
    static_assert(num_crc_streams >= 1 && num_crc_streams <= kMaxStreams,
                  "Invalid number of crc streams");
    static_assert(num_pclmul_streams >= 0 && num_pclmul_streams <= kMaxStreams,
                  "Invalid number of pclmul streams");
    constexpr StreamCounts kStreams = GetStreamCounts(copy_type);
    constexpr size_t kNumActiveCrcStreams = kStreams.crc;
    constexpr size_t kNumActivePclmulStreams = kStreams.pclmul;
    const uint8_t* p = static_cast<const uint8_t*>(bytes);
    const uint8_t* e = p + length;
    char* d = static_cast<char*>(dst);
    uint32_t l = *crc;
    uint64_t l64;

    if constexpr (copy_type != CopyType::kNone) {
      // Align the destination pointer to a cache line. Non-temporal stores
      // require the destination to be aligned to the store size, and aligning
      // regular stores avoids writes that straddle two cache lines.
      constexpr size_t kAlignment = 64;
      uintptr_t dst_addr = reinterpret_cast<uintptr_t>(d);
      size_t bytes_to_align =
          (kAlignment - (dst_addr & (kAlignment - 1))) & (kAlignment - 1);
      std::memcpy(d, p, bytes_to_align);
      d += bytes_to_align;
      while (bytes_to_align >= 8) {
        ABSL_INTERNAL_STEP8(l, p);
        bytes_to_align -= 8;
      }
      while (bytes_to_align > 0) {
        ABSL_INTERNAL_STEP1(l, p);
        bytes_to_align -= 1;
      }
    } else {
      // Point x at first 8-byte aligned byte in string.
      const uint8_t* x = RoundUp<8>(p);
      // Process bytes until p is 8-byte aligned, if that isn't past the end.
      while (p != x) {
        ABSL_INTERNAL_STEP1(l, p);
      }
    }

    constexpr size_t kBlockSize = 16;
    // We need to calculate a magic constant to multiply streams by in the final
    // reduction. Our table used for calculating this constant assumes the
    // least significant kNumDroppedBits bits are zero.
    static_assert(kBlockSize % (1 << kNumDroppedBits) == 0);
    constexpr size_t kBlocksPerIteration = ABSL_CACHELINE_SIZE / kBlockSize;
    static_assert(kBlocksPerIteration > 0);
    size_t blocks_per_stream =
        static_cast<size_t>(e - p) /
        (kNumActiveCrcStreams + kNumActivePclmulStreams) / kBlockSize;
    if constexpr (copy_type != CopyType::kNone) {
      // Round down to a whole number of main loop iterations, so that each
      // stream is a multiple of the cache line size.
      blocks_per_stream -= blocks_per_stream % kBlocksPerIteration;
    }
    const size_t stream_size = blocks_per_stream * kBlockSize;

    const uint8_t* stream_start = p;
    const uint8_t* crc_streams[kMaxStreams];
    for (size_t i = 0; i < kNumActiveCrcStreams; ++i) {
      crc_streams[i] = stream_start;
      stream_start += stream_size;
    }
    const uint8_t* pclmul_streams[kMaxStreams];
    for (size_t i = 0; i < kNumActivePclmulStreams; ++i) {
      pclmul_streams[i] = stream_start;
      stream_start += stream_size;
    }

    uint8_t* dst_crc_streams[kMaxStreams];
    uint8_t* dst_pclmul_streams[kMaxStreams];
    if constexpr (copy_type != CopyType::kNone) {
      uint8_t* dst_stream_start = reinterpret_cast<uint8_t*>(d);
      for (size_t i = 0; i < kNumActiveCrcStreams; ++i) {
        dst_crc_streams[i] = dst_stream_start;
        dst_stream_start += stream_size;
      }
      for (size_t i = 0; i < kNumActivePclmulStreams; ++i) {
        dst_pclmul_streams[i] = dst_stream_start;
        dst_stream_start += stream_size;
      }
    }

    // Per stream crc sums.
    uint64_t l64_crc[kMaxStreams] = {l};
    uint64_t l64_pclmul[kMaxStreams] = {0};

    V256 pclmul_v[kMaxStreams][2];
    for (size_t j = 0; j < kNumActivePclmulStreams; ++j) {
      V256_LoadPairU(pclmul_streams[j], &pclmul_v[j][0], &pclmul_v[j][1]);
    }

    if constexpr (copy_type != CopyType::kNone) {
      for (size_t j = 0; j < kNumActiveCrcStreams; ++j) {
        Copy64Bytes<copy_type>(dst_crc_streams[j], crc_streams[j]);
        dst_crc_streams[j] += 64;
      }
      for (size_t j = 0; j < kNumActivePclmulStreams; ++j) {
        Store64Bytes<copy_type>(dst_pclmul_streams[j], pclmul_v[j][0],
                                pclmul_v[j][1]);
        dst_pclmul_streams[j] += 64;
      }
    }

    for (size_t offset = 0; offset < 64; offset += 16) {
      for (size_t j = 0; j < kNumActiveCrcStreams; ++j) {
        l64_crc[j] = Process16BytesCRC(crc_streams[j] + offset, l64_crc[j]);
      }
    }
    for (size_t j = 0; j < kNumActiveCrcStreams; ++j) {
      crc_streams[j] += 64;
    }

    // Align to 32 bytes for vpclmul implementation.
    alignas(32) V128 partialCRC[kMaxStreams][4];
    for (size_t i = 0; i < kNumActivePclmulStreams; ++i) {
      InitPclmulStream(pclmul_v[i][0], pclmul_v[i][1], partialCRC[i]);
      pclmul_streams[i] += 64;
    }

    // Compute the magic constant for the final reduction early (even though
    // it's not needed until much later), so its latency is hidden by the main
    // loop.
    uint32_t magic = ComputeZeroConstant(stream_size);

    for (size_t i = 1; i < blocks_per_stream / kBlocksPerIteration; ++i) {
      // Prefetch data for next iterations.
      for (size_t j = 0; j < kNumActiveCrcStreams; ++j) {
        PrefetchToLocalCache(crc_streams[j] + kPrefetchHorizon);
        if constexpr (copy_type == CopyType::kTemporal && kShouldPrefetchDst) {
          PrefetchToLocalCache(dst_crc_streams[j] + kDstPrefetchHorizon);
        }
      }
      for (size_t j = 0; j < kNumActivePclmulStreams; ++j) {
        PrefetchToLocalCache(pclmul_streams[j] + kPrefetchHorizon);
        if constexpr (copy_type == CopyType::kTemporal && kShouldPrefetchDst) {
          PrefetchToLocalCache(dst_pclmul_streams[j] + kDstPrefetchHorizon);
        }
      }

      const uint8_t* pclmul_copy[kMaxStreams];
      if constexpr (copy_type != CopyType::kNone) {
        for (size_t j = 0; j < kNumActivePclmulStreams; ++j) {
          V256_LoadPairU(pclmul_streams[j], &pclmul_v[j][0], &pclmul_v[j][1]);
          Store64Bytes<copy_type>(dst_pclmul_streams[j], pclmul_v[j][0],
                                  pclmul_v[j][1]);
          dst_pclmul_streams[j] += 64;
          pclmul_streams[j] += 64;
          pclmul_copy[j] = reinterpret_cast<const uint8_t*>(pclmul_v[j]);
        }
      }
      const uint8_t** pclmul_src =
          copy_type != CopyType::kNone ? pclmul_copy : pclmul_streams;
      // Fold the four 16-byte blocks of each PCLMUL stream into
      // partialCRC[0..3]. For VPCLMUL, 32 bytes are folded every other block.
      ProcessAllStreams16<kNumActiveCrcStreams, kNumActivePclmulStreams,
                          /*kVpclmulBytes=*/32>(
          crc_streams, l64_crc, pclmul_src, partialCRC, /*lane=*/0);
      ProcessAllStreams16<kNumActiveCrcStreams, kNumActivePclmulStreams,
                          /*kVpclmulBytes=*/0>(crc_streams, l64_crc, pclmul_src,
                                               partialCRC, /*lane=*/1);
      ProcessAllStreams16<kNumActiveCrcStreams, kNumActivePclmulStreams,
                          /*kVpclmulBytes=*/32>(
          crc_streams, l64_crc, pclmul_src, partialCRC, /*lane=*/2);
      ProcessAllStreams16<kNumActiveCrcStreams, kNumActivePclmulStreams,
                          /*kVpclmulBytes=*/0>(crc_streams, l64_crc, pclmul_src,
                                               partialCRC, /*lane=*/3);

      // Copy the 64 bytes just processed only now, as whole cache lines,
      // instead of 16 bytes at a time as they are processed. Otherwise, when
      // the destination is slightly ahead of the source modulo 4096, the load
      // of the next 16 bytes of a stream would wait for the store of the
      // previous 16 bytes, since their addresses have the same lowest 12 bits
      // (4K aliasing).
      if constexpr (copy_type != CopyType::kNone) {
        for (size_t j = 0; j < kNumActiveCrcStreams; ++j) {
          Copy64Bytes<copy_type>(dst_crc_streams[j], crc_streams[j] - 64);
          dst_crc_streams[j] += 64;
        }
      }
    }

    // In copy mode, blocks_per_stream is a multiple of kBlocksPerIteration,
    // so the main loop processes every block and there are no remaining stream
    // blocks to process.
    if constexpr (copy_type == CopyType::kNone) {
      // Process the remaining bytes of every stream, if any. These
      // are folded into the oldest partial CRCs, which then hold the newest
      // bytes, so they are moved to the end to keep the partial CRCs in order.

      // We assume the remainder is one of 0, 16, 32, or 48 bytes (0-3 blocks).
      static_assert(kBlockSize == 16);
      static_assert(kBlockSize * kBlocksPerIteration == 64);
      if (blocks_per_stream & 2) {
        ProcessAllStreams16<kNumActiveCrcStreams, kNumActivePclmulStreams,
                            /*kVpclmulBytes=*/32>(
            crc_streams, l64_crc, pclmul_streams, partialCRC, /*lane=*/0);
        ProcessAllStreams16<kNumActiveCrcStreams, kNumActivePclmulStreams,
                            /*kVpclmulBytes=*/0>(
            crc_streams, l64_crc, pclmul_streams, partialCRC, /*lane=*/1);
        for (size_t j = 0; j < kNumActivePclmulStreams; ++j) {
          std::swap(partialCRC[j][0], partialCRC[j][2]);
          std::swap(partialCRC[j][1], partialCRC[j][3]);
        }
      }
      if (blocks_per_stream & 1) {
        ProcessAllStreams16<kNumActiveCrcStreams, kNumActivePclmulStreams,
                            /*kVpclmulBytes=*/16>(
            crc_streams, l64_crc, pclmul_streams, partialCRC, /*lane=*/0);
        for (size_t j = 0; j < kNumActivePclmulStreams; ++j) {
          V128 newest = partialCRC[j][0];
          partialCRC[j][0] = partialCRC[j][1];
          partialCRC[j][1] = partialCRC[j][2];
          partialCRC[j][2] = partialCRC[j][3];
          partialCRC[j][3] = newest;
        }
      }
    }

    // PCLMULQDQ based streams require special final step;
    // CRC based don't.
    for (size_t i = 0; i < kNumActivePclmulStreams; ++i) {
      l64_pclmul[i] = FinalizePclmulStream(partialCRC[i]);
    }

    // Combine all streams into single result.
    l64 = l64_crc[0];
    for (size_t i = 1; i < kNumActiveCrcStreams; ++i) {
      l64 = MultiplyWithExtraX33(static_cast<uint32_t>(l64), magic);
      l64 ^= l64_crc[i];
    }
    for (size_t i = 0; i < kNumActivePclmulStreams; ++i) {
      l64 = MultiplyWithExtraX33(static_cast<uint32_t>(l64), magic);
      l64 ^= l64_pclmul[i];
    }

    // Update p and d.
    if constexpr (kNumActivePclmulStreams > 0) {
      p = pclmul_streams[kNumActivePclmulStreams - 1];
      if constexpr (copy_type != CopyType::kNone) {
        d = reinterpret_cast<char*>(
            dst_pclmul_streams[kNumActivePclmulStreams - 1]);
        size_t remaining_to_copy = static_cast<size_t>(e - p);
        std::memcpy(d, p, remaining_to_copy);
      }
    } else {
      p = crc_streams[kNumActiveCrcStreams - 1];
      if constexpr (copy_type != CopyType::kNone) {
        d = reinterpret_cast<char*>(dst_crc_streams[kNumActiveCrcStreams - 1]);
        size_t remaining_to_copy = static_cast<size_t>(e - p);
        std::memcpy(d, p, remaining_to_copy);
      }
    }

    l = static_cast<uint32_t>(l64);

    uint64_t remaining_bytes = static_cast<uint64_t>(e - p);
    // Process the remaining bytes.
    while ((e - p) >= 16) {
      ABSL_INTERNAL_STEP8(l, p);
      ABSL_INTERNAL_STEP8(l, p);
    }

    if (remaining_bytes & 8) {
      ABSL_INTERNAL_STEP8(l, p);
    }
    if (remaining_bytes & 4) {
      ABSL_INTERNAL_STEP4(l, p);
    }
    if (remaining_bytes & 2) {
      ABSL_INTERNAL_STEP2(l, p);
    }
    if (remaining_bytes & 1) {
      ABSL_INTERNAL_STEP1(l, p);
    }

    *crc = l;

    if constexpr (copy_type == CopyType::kNonTemporal) {
      // According to the Intel 64 Software Developer's Manual
      // (https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html),
      // non-temporal stores are weakly ordered due to bypassing caches, so
      // they have to be fenced before the copy is visible to other threads.
      // Regular stores are ordered and need no fence. Note that the fence is
      // placed after all stores, to avoid unnecessary serialization and a
      // performance hit.
      NonTemporalStoreFence();
    }
  }

 private:
  struct StreamCounts {
    size_t crc;
    size_t pclmul;
  };

  // Returns the number of crc and pclmul streams to use. Copying adds loads and
  // stores, so fewer streams are needed to saturate the dispatch width. In copy
  // mode, at most kMaxCopyStreams of the configured streams are used, taken in
  // the order: pclmul 1, crc 1, pclmul 2, crc 2, pclmul 3, crc 3.
  static constexpr StreamCounts GetStreamCounts(CopyType copy_type) {
    if (copy_type == CopyType::kNone) {
      return {num_crc_streams, num_pclmul_streams};
    }
    StreamCounts counts = {0, 0};
    for (size_t i = 0; i < kMaxStreams; ++i) {
      if (i < num_pclmul_streams &&
          counts.crc + counts.pclmul < kMaxCopyStreams) {
        counts.pclmul++;
      }
      if (i < num_crc_streams && counts.crc + counts.pclmul < kMaxCopyStreams) {
        counts.crc++;
      }
    }
    return counts;
  }

  // Whether PCLMUL streams are folded with 256-bit `vpclmulqdq`.
  static constexpr bool kIsVpclmul =
      (pclmul_stream_type == PclmulStreamType::VPCLMUL);

  // The high product of a fold, which is returned by `PclmulStream` and
  // consumed by `XorPclmulStream`. Folding 32 bytes with 256-bit `vpclmulqdq`
  // produces a V256; every other PclmulStreamType produces a V128.
  template <size_t kVpclmulBytes>
  using PclmulProduct =
      std::conditional_t<kIsVpclmul && kVpclmulBytes == 32, V256, V128>;

  ABSL_INTERNAL_ATTRIBUTE_AVX ABSL_ATTRIBUTE_ALWAYS_INLINE void
  InitPclmulStream(V256 v0, V256 v1, V128* partialCRC) const {
    if constexpr (pclmul_stream_type == PclmulStreamType::NONE) {
      return;
    } else if constexpr (kIsVpclmul) {
      V256* vpartialCRC = reinterpret_cast<V256*>(partialCRC);
      vpartialCRC[0] = v0;
      vpartialCRC[1] = v1;
    } else {
      const V128* data_ptr0 = reinterpret_cast<const V128*>(&v0);
      const V128* data_ptr1 = reinterpret_cast<const V128*>(&v1);
      partialCRC[0] = data_ptr0[0];
      partialCRC[1] = data_ptr0[1];
      partialCRC[2] = data_ptr1[0];
      partialCRC[3] = data_ptr1[1];
    }
  }

  // Performs the polynomial multiplications to fold `partialCRC` by 512 bits,
  // storing the low product in `partialCRC` and returning the high product,
  // which must be passed to `XorPclmulStream` to complete the fold.
  template <size_t kVpclmulBytes>
  ABSL_INTERNAL_ATTRIBUTE_AVX ABSL_ATTRIBUTE_ALWAYS_INLINE
      PclmulProduct<kVpclmulBytes>
      PclmulStream(V128* partialCRC) const {
    if constexpr (pclmul_stream_type == PclmulStreamType::NONE ||
                  (kIsVpclmul && kVpclmulBytes == 0)) {
      return {};
    } else if constexpr (kIsVpclmul && kVpclmulBytes == 32) {
      return Pclmul32BytesVpclmul(reinterpret_cast<V256*>(partialCRC));
    } else {
      return Pclmul16Bytes(partialCRC);
    }
  }

  // Completes folding the next bytes at `*stream` into `partialCRC` by XORing
  // the high product returned by `PclmulStream`, the low product in
  // `partialCRC`, and the loaded data, and advances `*stream`.
  template <size_t kVpclmulBytes>
  ABSL_INTERNAL_ATTRIBUTE_AVX ABSL_ATTRIBUTE_ALWAYS_INLINE void XorPclmulStream(
      const uint8_t** stream, const PclmulProduct<kVpclmulBytes>& high,
      V128* partialCRC) const {
    if constexpr (pclmul_stream_type == PclmulStreamType::NONE ||
                  (kIsVpclmul && kVpclmulBytes == 0)) {
      return;
    } else if constexpr (kIsVpclmul && kVpclmulBytes == 32) {
      Xor32BytesVpclmul(*stream, high, reinterpret_cast<V256*>(partialCRC));
      *stream += 32;
    } else {
      V128 data = V128_LoadU(reinterpret_cast<const V128*>(*stream));
      if constexpr (pclmul_stream_type == PclmulStreamType::NEON_PCLMUL ||
                    pclmul_stream_type == PclmulStreamType::NEON_PCLMUL_EOR3) {
        constexpr bool kUseEor3 =
            (pclmul_stream_type == PclmulStreamType::NEON_PCLMUL_EOR3);
        Xor16BytesNeonPclmul<kUseEor3>(data, high, partialCRC);
      } else {
        static_assert(pclmul_stream_type == PclmulStreamType::PCLMUL ||
                      (kIsVpclmul && kVpclmulBytes == 16));
        Xor16BytesPclmul(data, high, partialCRC);
      }
      *stream += 16;
    }
  }

  // Processes the next 16 bytes of every CRC and PCLMUL stream, folding each
  // PCLMUL stream into `partialCRC[lane]`, and advances the stream pointers.
  //
  // PCLMUL, vector XOR, and CRC32 instructions are interleaved across the two
  // 8-byte halves of each 16-byte CRC step (8B CRC -> PCLMUL -> 8B CRC -> XOR)
  // to keep the execution units busy.
  //
  // When `pclmul_stream_type` is `VPCLMUL`, a single 256-bit `vpclmulqdq`
  // instruction folds 32 bytes at once across two 16-byte calls.
  // `kVpclmulBytes` controls how many bytes of each VPCLMUL stream to process
  // in this call:
  //   - 32: Fold 32 bytes into `partialCRC[lane..lane + 1]` (used on the first
  //         half of a 32-byte block).
  //   -  0: Skip the VPCLMUL streams (used on the second half of a 32-byte
  //         block, since the first half already folded all 32 bytes).
  //   - 16: Fold 16 bytes into `partialCRC[lane]` using 128-bit `pclmulqdq`
  //         (used for the standalone 16-byte tail).
  // For non-VPCLMUL stream types, `kVpclmulBytes` is ignored and 16 bytes are
  // always folded.
  template <size_t kNumActiveCrcStreams, size_t kNumActivePclmulStreams,
            size_t kVpclmulBytes>
  ABSL_INTERNAL_ATTRIBUTE_AVX ABSL_ATTRIBUTE_ALWAYS_INLINE void
  ProcessAllStreams16(const uint8_t** crc_streams, uint64_t* l64_crc,
                      const uint8_t** pclmul_streams, V128 (*partialCRC)[4],
                      size_t lane) const {
    for (size_t j = 0; j < kMaxStreams; ++j) {
      if (j < kNumActiveCrcStreams) {
        l64_crc[j] = Process8BytesCRC(crc_streams[j], l64_crc[j]);
        crc_streams[j] += 8;
      }
      PclmulProduct<kVpclmulBytes> high;
      if (j < kNumActivePclmulStreams) {
        high = PclmulStream<kVpclmulBytes>(&partialCRC[j][lane]);
      }
      if (j < kNumActiveCrcStreams) {
        l64_crc[j] = Process8BytesCRC(crc_streams[j], l64_crc[j]);
        crc_streams[j] += 8;
      }
      if (j < kNumActivePclmulStreams) {
        XorPclmulStream<kVpclmulBytes>(&pclmul_streams[j], high,
                                       &partialCRC[j][lane]);
      }
    }
  }

  ABSL_INTERNAL_ATTRIBUTE_AVX ABSL_ATTRIBUTE_ALWAYS_INLINE uint64_t
  FinalizePclmulStream(V128* partialCRC) const {
    if constexpr (pclmul_stream_type == PclmulStreamType::NONE) {
      return 0;
    } else if constexpr (kIsVpclmul) {
      return FinalizeVpclmulStream(reinterpret_cast<V256*>(partialCRC));
    } else if constexpr (pclmul_stream_type == PclmulStreamType::NEON_PCLMUL ||
                         pclmul_stream_type ==
                             PclmulStreamType::NEON_PCLMUL_EOR3) {
      constexpr bool kUseEor3 =
          (pclmul_stream_type == PclmulStreamType::NEON_PCLMUL_EOR3);
      return FinalizeNeonPclmulStream<kUseEor3>(partialCRC);
    } else {
      return CRC32AcceleratedX86ARMCombinedMultipleStreamsBase::
          FinalizePclmulStream(partialCRC);
    }
  }
};

#undef ABSL_INTERNAL_STEP8BY3
#undef ABSL_INTERNAL_STEP8BY2
#undef ABSL_INTERNAL_STEP8
#undef ABSL_INTERNAL_STEP4
#undef ABSL_INTERNAL_STEP2
#undef ABSL_INTERNAL_STEP1

}  // namespace

#if defined(__clang__)
#pragma clang attribute pop
#else
#pragma GCC pop_options
#endif

namespace {

#if defined(__x86_64__)
// The VPCLMUL engines need AVX2 and VPCLMULQDQ.
bool SupportsVpclmul() {
  __builtin_cpu_init();
  return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("vpclmulqdq");
}
#else
// The NEON_PCLMUL_EOR3 engines need the EOR3 instruction of FEAT_SHA3.
bool SupportsArmSha3() {
#if defined(__linux__) && defined(HWCAP_SHA3)
  return (getauxval(AT_HWCAP) & HWCAP_SHA3) != 0;
#elif defined(__APPLE__)
  int sha3 = 0;
  size_t size = sizeof(sha3);
  return sysctlbyname("hw.optional.arm.FEAT_SHA3", &sha3, &size, nullptr, 0) == 0 && sha3;
#else
  return false;
#endif
}
#endif

// The tuning of some CPU models below needs features beyond the model's baseline. A hypervisor may
// report the model while it hides some of its features, for example to let a VM migrate to an
// older host, so such a model gets the tuning of a similar model without those features.
CpuType AdjustForMissingFeatures(CpuType type) {
#if defined(__x86_64__)
  if (SupportsVpclmul()) {
    return type;
  }
  switch (type) {
    case CpuType::kIntelSapphirerapids:
    case CpuType::kIntelEmeraldrapids:
    case CpuType::kIntelGraniterapids:
      return CpuType::kIntelIcelake;
    case CpuType::kAmdMilan:
    case CpuType::kAmdGenoa:
    case CpuType::kAmdRyzenV3000:
    case CpuType::kAmdSiena:
    case CpuType::kAmdTurin:
    case CpuType::kAmdVenice:
      return CpuType::kAmdRome;
    default:
      return type;
  }
#else
  if (SupportsArmSha3()) {
    return type;
  }
  switch (type) {
    case CpuType::kArmNeoverseN2:
    case CpuType::kArmNeoverseN3:
    case CpuType::kArmNeoverseN4:
    case CpuType::kNvidiaGrace:
    case CpuType::kAmpereSiryn:
    case CpuType::kArmNeoverseV2:
    case CpuType::kArmNeoverseV3:
      return CpuType::kUnknown;
    default:
      return type;
  }
#endif
}

}  // namespace

// Intel processors with SSE4.2 have an instruction for one particular
// 32-bit CRC polynomial:  crc32c
Crc32cEngine* TryNewCRC32AcceleratedX86ARMCombined() {
  CpuType type = AdjustForMissingFeatures(GetCpuType());
  switch (type) {
    case CpuType::kIntelHaswell:
    case CpuType::kAmdNaples:
    case CpuType::kAmdRome:
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          3, 1, PclmulStreamType::PCLMUL>();
    case CpuType::kAmdMilan:
    case CpuType::kAmdGenoa:
    case CpuType::kAmdRyzenV3000:
    case CpuType::kAmdSiena:
    case CpuType::kAmdTurin:
    case CpuType::kAmdVenice:
#if defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD) &&                   \
    (defined(__AVX__) || defined(ABSL_INTERNAL_CAN_FORCE_AVX)) && \
    (!defined(_MSC_VER) || defined(__clang__))
      // We don't have vector pclmul on arm, but this still needs to
      // compile.
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          2, 3, PclmulStreamType::VPCLMUL>();
#else
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          3, 1, PclmulStreamType::PCLMUL>();
#endif
    case CpuType::kIntelSapphirerapids:
    case CpuType::kIntelEmeraldrapids:
    case CpuType::kIntelGraniterapids:
#if defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD) &&                   \
    (defined(__AVX__) || defined(ABSL_INTERNAL_CAN_FORCE_AVX)) && \
    (!defined(_MSC_VER) || defined(__clang__))
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          3, 3, PclmulStreamType::VPCLMUL>();
#else
      [[fallthrough]];
#endif
    // PCLMULQDQ is fast, use combined PCLMULQDQ + CRC implementation.
    case CpuType::kIntelBroadwell:
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          3, 2, PclmulStreamType::PCLMUL>();
    case CpuType::kIntelCascadelakeXeon:
    case CpuType::kIntelSkylakeXeon:
    case CpuType::kIntelSkylake:
    case CpuType::kIntelIcelake:
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          3, 3, PclmulStreamType::PCLMUL>();
    // PCLMULQDQ is slow, don't use it.
    case CpuType::kIntelIvybridge:
    case CpuType::kIntelSandybridge:
    case CpuType::kIntelWestmere:
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          3, 0, PclmulStreamType::NONE>();
    case CpuType::kArmNeoverseN1:
    case CpuType::kArmNeoverseV1:
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          1, 1, PclmulStreamType::NEON_PCLMUL>();
    case CpuType::kArmNeoverseN2:
    case CpuType::kArmNeoverseN3:
    case CpuType::kArmNeoverseN4:
    case CpuType::kNvidiaGrace:
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          1, 1, PclmulStreamType::NEON_PCLMUL_EOR3>();
    case CpuType::kAmpereSiryn:
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          3, 2, PclmulStreamType::NEON_PCLMUL_EOR3>();
    case CpuType::kArmNeoverseV2:
    case CpuType::kArmNeoverseV3:
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          1, 2, PclmulStreamType::NEON_PCLMUL_EOR3>();
#if defined(__aarch64__)
    default:
      // Not all ARM processors support the needed instructions, so check here
      // before trying to use an accelerated implementation.
      if (SupportsArmCRC32PMULL()) {
#if defined(__APPLE__)
        return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
            3, 3, PclmulStreamType::NEON_PCLMUL>();
#else
        return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
            1, 1, PclmulStreamType::NEON_PCLMUL>();
#endif
      } else {
        return nullptr;
      }
#else
    default:
      // VPCLMULQDQ first shipped on Ice Lake and Zen 3, and every CPU with it has fast PCLMULQDQ,
      // so the CPUs that have it and are not listed above get the Ice Lake tuning.
      __builtin_cpu_init();
      if (__builtin_cpu_supports("vpclmulqdq")) {
        return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
            3, 3, PclmulStreamType::PCLMUL>();
      }
      // Something else, play it safe and assume slow PCLMULQDQ.
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          3, 0, PclmulStreamType::NONE>();
#endif
  }
}

namespace {

template <size_t num_crc_streams, size_t num_pclmul_streams,
          PclmulStreamType pclmul_stream_type>
void AddEngineForTests(const char* name, std::vector<NamedCrc32cEngine>* engines) {
  engines->push_back({name, std::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
      num_crc_streams, num_pclmul_streams, pclmul_stream_type>>()});
}

}  // namespace

std::vector<NamedCrc32cEngine> NewAbseilEnginesForTests() {
  std::vector<NamedCrc32cEngine> result;
#if defined(__x86_64__)
  __builtin_cpu_init();
  if (!__builtin_cpu_supports("sse4.2") || !__builtin_cpu_supports("pclmul") ||
      !__builtin_cpu_supports("avx")) {
    return result;
  }
  AddEngineForTests<3, 0, PclmulStreamType::NONE>("abseil<3, 0, NONE>", &result);
  AddEngineForTests<3, 1, PclmulStreamType::PCLMUL>("abseil<3, 1, PCLMUL>", &result);
  AddEngineForTests<3, 2, PclmulStreamType::PCLMUL>("abseil<3, 2, PCLMUL>", &result);
  AddEngineForTests<3, 3, PclmulStreamType::PCLMUL>("abseil<3, 3, PCLMUL>", &result);
#if (defined(__AVX__) || defined(ABSL_INTERNAL_CAN_FORCE_AVX)) && \
    (!defined(_MSC_VER) || defined(__clang__))
  if (SupportsVpclmul()) {
    AddEngineForTests<2, 3, PclmulStreamType::VPCLMUL>("abseil<2, 3, VPCLMUL>", &result);
    AddEngineForTests<3, 3, PclmulStreamType::VPCLMUL>("abseil<3, 3, VPCLMUL>", &result);
  }
#endif
#else
  if (!SupportsArmCRC32PMULL()) {
    return result;
  }
  AddEngineForTests<1, 1, PclmulStreamType::NEON_PCLMUL>("abseil<1, 1, NEON_PCLMUL>", &result);
  AddEngineForTests<3, 3, PclmulStreamType::NEON_PCLMUL>("abseil<3, 3, NEON_PCLMUL>", &result);
  if (SupportsArmSha3()) {
    AddEngineForTests<1, 1, PclmulStreamType::NEON_PCLMUL_EOR3>(
        "abseil<1, 1, NEON_PCLMUL_EOR3>", &result);
    AddEngineForTests<1, 2, PclmulStreamType::NEON_PCLMUL_EOR3>(
        "abseil<1, 2, NEON_PCLMUL_EOR3>", &result);
    AddEngineForTests<3, 2, PclmulStreamType::NEON_PCLMUL_EOR3>(
        "abseil<3, 2, NEON_PCLMUL_EOR3>", &result);
  }
#endif
  return result;
}

#else  // !ABSL_INTERNAL_CAN_USE_SIMD_CRC32C

// no hardware acceleration available
Crc32cEngine* TryNewCRC32AcceleratedX86ARMCombined() { return nullptr; }

std::vector<NamedCrc32cEngine> NewAbseilEnginesForTests() { return {}; }

#endif

}  // namespace yb::crc::internal
