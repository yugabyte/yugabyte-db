/**
 *  @file       binary.h
 *  @brief      SIMD-accelerated Binary Similarity Measures.
 *  @author     Ash Vardanian
 *  @date       July 1, 2023
 *
 *  Contains:
 *  - Hamming distance
 *  - Jaccard similarity (Tanimoto coefficient)
 *
 *  For hardware architectures:
 *  - Arm (NEON, SVE)
 *  - x86 (AVX2, AVX512)
 *
 *  x86 intrinsics: https://www.intel.com/content/www/us/en/docs/intrinsics-guide/
 *  Arm intrinsics: https://developer.arm.com/architectures/instruction-sets/intrinsics/
 */
#ifndef SIMSIMD_BINARY_H
#define SIMSIMD_BINARY_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off

/*  Serial backends for bitsets. */
SIMSIMD_PUBLIC void simsimd_hamming_b8_serial(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words, simsimd_distance_t* distance);
SIMSIMD_PUBLIC void simsimd_jaccard_b8_serial(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words, simsimd_distance_t* distance);

/*  Arm NEON backend for bitsets. */
SIMSIMD_PUBLIC void simsimd_hamming_b8_neon(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words, simsimd_distance_t* distance);
SIMSIMD_PUBLIC void simsimd_jaccard_b8_neon(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words, simsimd_distance_t* distance);

/*  Arm SVE backend for bitsets. */
SIMSIMD_PUBLIC void simsimd_hamming_b8_sve(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words, simsimd_distance_t* distance);
SIMSIMD_PUBLIC void simsimd_jaccard_b8_sve(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words, simsimd_distance_t* distance);

/*  x86 AVX2 backend for bitsets for Intel Haswell CPUs and newer, needs only POPCNT extensions. */
SIMSIMD_PUBLIC void simsimd_hamming_b8_haswell(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words, simsimd_distance_t* distance);
SIMSIMD_PUBLIC void simsimd_jaccard_b8_haswell(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words, simsimd_distance_t* distance);

/*  x86 AVX512 backend for bitsets for Intel Ice Lake CPUs and newer, using VPOPCNTDQ extensions. */
SIMSIMD_PUBLIC void simsimd_hamming_b8_ice(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words, simsimd_distance_t* distance);
SIMSIMD_PUBLIC void simsimd_jaccard_b8_ice(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words, simsimd_distance_t* distance);
// clang-format on

SIMSIMD_PUBLIC unsigned char simsimd_popcount_b8(simsimd_b8_t x) {
    static unsigned char lookup_table[] = {
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, //
        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
        1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
        2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
        3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8};
    return lookup_table[x];
}

SIMSIMD_PUBLIC void simsimd_hamming_b8_serial(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words,
                                              simsimd_distance_t* result) {
    simsimd_i32_t differences = 0;
    for (simsimd_size_t i = 0; i != n_words; ++i)
        differences += simsimd_popcount_b8(a[i] ^ b[i]);
    *result = differences;
}

SIMSIMD_PUBLIC void simsimd_jaccard_b8_serial(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words,
                                              simsimd_distance_t* result) {
    simsimd_i32_t intersection = 0, union_ = 0;
    for (simsimd_size_t i = 0; i != n_words; ++i)
        intersection += simsimd_popcount_b8(a[i] & b[i]), union_ += simsimd_popcount_b8(a[i] | b[i]);
    *result = (union_ != 0) ? 1 - (simsimd_f64_t)intersection / (simsimd_f64_t)union_ : 1;
}

#if SIMSIMD_TARGET_ARM
#if SIMSIMD_TARGET_NEON
#pragma GCC push_options
#pragma GCC target("arch=armv8.2-a+simd")
#ifdef __clang__
#pragma clang attribute push(__attribute__((target("arch=armv8.2-a+simd"))), apply_to = function)

#endif

SIMSIMD_PUBLIC void simsimd_hamming_b8_neon(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words,
                                            simsimd_distance_t* result) {
    simsimd_i32_t differences = 0;
    simsimd_size_t i = 0;
    for (; i + 16 <= n_words; i += 16) {
        uint8x16_t a_first = vld1q_u8(a + i);
        uint8x16_t b_first = vld1q_u8(b + i);
        differences += vaddvq_u8(vcntq_u8(veorq_u8(a_first, b_first)));
    }
    // Handle the tail
    for (; i != n_words; ++i)
        differences += simsimd_popcount_b8(a[i] ^ b[i]);
    *result = differences;
}

SIMSIMD_PUBLIC void simsimd_jaccard_b8_neon(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words,
                                            simsimd_distance_t* result) {
    simsimd_i32_t intersection = 0, union_ = 0;
    simsimd_size_t i = 0;
    for (; i + 16 <= n_words; i += 16) {
        uint8x16_t a_first = vld1q_u8(a + i);
        uint8x16_t b_first = vld1q_u8(b + i);
        intersection += vaddvq_u8(vcntq_u8(vandq_u8(a_first, b_first)));
        union_ += vaddvq_u8(vcntq_u8(vorrq_u8(a_first, b_first)));
    }
    // Handle the tail
    for (; i != n_words; ++i)
        intersection += simsimd_popcount_b8(a[i] & b[i]), union_ += simsimd_popcount_b8(a[i] | b[i]);
    *result = (union_ != 0) ? 1 - (simsimd_f64_t)intersection / (simsimd_f64_t)union_ : 1;
}

#ifdef __clang__
#pragma clang attribute pop
#endif
#pragma GCC pop_options
#endif // SIMSIMD_TARGET_NEON

#if SIMSIMD_TARGET_SVE
#pragma GCC push_options
#pragma GCC target("arch=armv8.2-a+sve")
#ifdef __clang__
#pragma clang attribute push(__attribute__((target("arch=armv8.2-a+sve"))), apply_to = function)

#endif

SIMSIMD_PUBLIC void simsimd_hamming_b8_sve(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words,
                                           simsimd_distance_t* result) {
    simsimd_size_t i = 0;
    simsimd_i32_t differences = 0;
    do {
        svbool_t pg_vec = svwhilelt_b8((unsigned int)i, (unsigned int)n_words);
        svuint8_t a_vec = svld1_u8(pg_vec, a + i);
        svuint8_t b_vec = svld1_u8(pg_vec, b + i);
        differences += svaddv_u8(svptrue_b8(), svcnt_u8_x(svptrue_b8(), sveor_u8_m(svptrue_b8(), a_vec, b_vec)));
        i += svcntb();
    } while (i < n_words);
    *result = differences;
}

SIMSIMD_PUBLIC void simsimd_jaccard_b8_sve(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words,
                                           simsimd_distance_t* result) {
    simsimd_size_t i = 0;
    simsimd_i32_t intersection = 0, union_ = 0;
    do {
        svbool_t pg_vec = svwhilelt_b8((unsigned int)i, (unsigned int)n_words);
        svuint8_t a_vec = svld1_u8(pg_vec, a + i);
        svuint8_t b_vec = svld1_u8(pg_vec, b + i);
        intersection += svaddv_u8(svptrue_b8(), svcnt_u8_x(svptrue_b8(), svand_u8_m(svptrue_b8(), a_vec, b_vec)));
        union_ += svaddv_u8(svptrue_b8(), svcnt_u8_x(svptrue_b8(), svorr_u8_m(svptrue_b8(), a_vec, b_vec)));
        i += svcntb();
    } while (i < n_words);
    *result = (union_ != 0) ? 1 - (simsimd_f64_t)intersection / (simsimd_f64_t)union_ : 1;
}

#ifdef __clang__
#pragma clang attribute pop
#endif
#pragma GCC pop_options
#endif // SIMSIMD_TARGET_SVE
#endif // SIMSIMD_TARGET_ARM

#if SIMSIMD_TARGET_X86
#if SIMSIMD_TARGET_ICE
#pragma GCC push_options
#pragma GCC target("avx2", "avx512f", "avx512vl", "bmi2", "avx512bw", "avx512vpopcntdq")
#ifdef __clang__
#pragma clang attribute push(__attribute__((target("avx2,avx512f,avx512vl,bmi2,avx512bw,avx512vpopcntdq"))),           \
                             apply_to = function)

#endif

SIMSIMD_PUBLIC void simsimd_hamming_b8_ice(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words,
                                           simsimd_distance_t* result) {
    __m512i differences_vec = _mm512_setzero_si512();
    __m512i a_vec, b_vec;

simsimd_hamming_b8_ice_cycle:
    if (n_words < 64) {
        __mmask64 mask = (__mmask64)_bzhi_u64(0xFFFFFFFFFFFFFFFF, n_words);
        a_vec = _mm512_maskz_loadu_epi8(mask, a);
        b_vec = _mm512_maskz_loadu_epi8(mask, b);
        n_words = 0;
    } else {
        a_vec = _mm512_loadu_epi8(a);
        b_vec = _mm512_loadu_epi8(b);
        a += 64, b += 64, n_words -= 64;
    }
    __m512i xor_vec = _mm512_xor_si512(a_vec, b_vec);
    differences_vec = _mm512_add_epi64(differences_vec, _mm512_popcnt_epi64(xor_vec));
    if (n_words)
        goto simsimd_hamming_b8_ice_cycle;

    simsimd_size_t differences = _mm512_reduce_add_epi64(differences_vec);
    *result = differences;
}

SIMSIMD_PUBLIC void simsimd_jaccard_b8_ice(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words,
                                           simsimd_distance_t* result) {
    __m512i intersection_vec = _mm512_setzero_si512(), union_vec = _mm512_setzero_si512();
    __m512i a_vec, b_vec;

simsimd_jaccard_b8_ice_cycle:
    if (n_words < 64) {
        __mmask64 mask = (__mmask64)_bzhi_u64(0xFFFFFFFFFFFFFFFF, n_words);
        a_vec = _mm512_maskz_loadu_epi8(mask, a);
        b_vec = _mm512_maskz_loadu_epi8(mask, b);
        n_words = 0;
    } else {
        a_vec = _mm512_loadu_epi8(a);
        b_vec = _mm512_loadu_epi8(b);
        a += 64, b += 64, n_words -= 64;
    }
    __m512i and_vec = _mm512_and_si512(a_vec, b_vec);
    __m512i or_vec = _mm512_or_si512(a_vec, b_vec);
    intersection_vec = _mm512_add_epi64(intersection_vec, _mm512_popcnt_epi64(and_vec));
    union_vec = _mm512_add_epi64(union_vec, _mm512_popcnt_epi64(or_vec));
    if (n_words)
        goto simsimd_jaccard_b8_ice_cycle;

    simsimd_size_t intersection = _mm512_reduce_add_epi64(intersection_vec),
                   union_ = _mm512_reduce_add_epi64(union_vec);
    *result = (union_ != 0) ? 1 - (simsimd_f64_t)intersection / (simsimd_f64_t)union_ : 1;
}

#ifdef __clang__
#pragma clang attribute pop
#endif
#pragma GCC pop_options
#endif // SIMSIMD_TARGET_ICE

#if SIMSIMD_TARGET_HASWELL
#pragma GCC push_options
#pragma GCC target("popcnt")
#ifdef __clang__
#pragma clang attribute push(__attribute__((target("popcnt"))), apply_to = function)

#endif

SIMSIMD_PUBLIC void simsimd_hamming_b8_haswell(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words,
                                               simsimd_distance_t* result) {
    // x86 supports unaligned loads and works just fine with the scalar version for small vectors.
    simsimd_size_t differences = 0;
    for (; n_words >= 8; n_words -= 8, a += 8, b += 8)
        differences += _mm_popcnt_u64(*(simsimd_u64_t const*)a ^ *(simsimd_u64_t const*)b);
    for (; n_words; --n_words, ++a, ++b)
        differences += _mm_popcnt_u32(*a ^ *b);
    *result = differences;
}

SIMSIMD_PUBLIC void simsimd_jaccard_b8_haswell(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n_words,
                                               simsimd_distance_t* result) {
    // x86 supports unaligned loads and works just fine with the scalar version for small vectors.
    simsimd_size_t intersection = 0, union_ = 0;
    for (; n_words >= 8; n_words -= 8, a += 8, b += 8)
        intersection += _mm_popcnt_u64(*(simsimd_u64_t const*)a & *(simsimd_u64_t const*)b),
            union_ += _mm_popcnt_u64(*(simsimd_u64_t const*)a | *(simsimd_u64_t const*)b);
    for (; n_words; --n_words, ++a, ++b)
        intersection += _mm_popcnt_u32(*a & *b), union_ += _mm_popcnt_u32(*a | *b);
    *result = (union_ != 0) ? 1 - (simsimd_f64_t)intersection / (simsimd_f64_t)union_ : 1;
}

#ifdef __clang__
#pragma clang attribute pop
#endif
#pragma GCC pop_options
#endif // SIMSIMD_TARGET_HASWELL
#endif // SIMSIMD_TARGET_X86

#ifdef __cplusplus
}
#endif

#endif

// This file is part of the simsimd inline third-party dependency of YugabyteDB.
// Git repo: https://github.com/yugabyte/simsimd
// Git tag: v5.4.3-yb-1
//
// See also src/inline-thirdparty/README.md.
