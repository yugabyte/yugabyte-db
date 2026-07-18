/**
 *  @file       simsimd.h
 *  @brief      SIMD-accelerated Similarity Measures and Distance Functions.
 *  @author     Ash Vardanian
 *  @date       March 14, 2023
 *  @copyright  Copyright (c) 2023
 *
 *  References:
 *  x86 intrinsics: https://www.intel.com/content/www/us/en/docs/intrinsics-guide
 *  Arm intrinsics: https://developer.arm.com/architectures/instruction-sets/intrinsics
 *  Detecting target CPU features at compile time: https://stackoverflow.com/a/28939692/2766161
 *
 *  @section    Choosing x86 Target Generations
 *
 *  It's important to provide fine-grained controls over AVX512 families, as they are very fragmented:
 *
 *  - Intel Skylake servers: F, CD, VL, DQ, BW
 *  - Intel Cascade Lake workstations: F, CD, VL, DQ, BW, VNNI
 *       > In other words, it extends Skylake with VNNI support
 *  - Intel Sunny Cove (Ice Lake) servers:
 *         F, CD, VL, DQ, BW, VNNI, VPOPCNTDQ, IFMA, VBMI, VAES, GFNI, VBMI2, BITALG, VPCLMULQDQ
 *  - AMD Zen4 (Genoa):
 *         F, CD, VL, DQ, BW, VNNI, VPOPCNTDQ, IFMA, VBMI, VAES, GFNI, VBMI2, BITALG, VPCLMULQDQ, BF16
 *       > In other words, it extends Sunny Cove with BF16 support
 *  - Intel Golden Cove (Sapphire Rapids): extends Zen4 and Sunny Cove with FP16 support
 *
 *  Intel Palm Cove was an irrelevant intermediate release extending Skylake with IFMA and VBMI.
 *  Intel Willow Cove was an irrelevant intermediate release extending Sunny Cove with VP2INTERSECT,
 *  that aren't supported by any other CPU built to date... and those are only available in Tiger Lake laptops.
 *  Intel Cooper Lake was the only intermediary platform, that supported BF16, but not FP16.
 *  It's mostly used in 4-socket and 8-socket high-memory configurations.
 *
 *  In practical terms, it makes sense to differentiate only 3 AVX512 generations:
 *  1. Intel Skylake (pre 2019): supports single-precision dot-products.
 *  2. Intel Ice Lake (2019-2021): advanced integer algorithms.
 *  3. AMD Genoa (2023+): brain-floating point support.
 *  4. Intel Sapphire Rapids (2023+): advanced mixed-precision float processing.
 *
 *  To list all available macros for x86, take a recent compiler, like GCC 12 and run:
 *       gcc-12 -march=sapphirerapids -dM -E - < /dev/null | egrep "SSE|AVX" | sort
 *  On Arm machines you may want to check for other flags:
 *       gcc-12 -march=native -dM -E - < /dev/null | egrep "NEON|SVE|FP16|FMA" | sort
 *
 *  @section    Choosing Arm Target Generations
 *
 *  Arm CPUs share design IP, but are produced by different vendors, potentially making the platform
 *  even more fragmented than x86. There are 2 important families of SIMD extensions - NEON and SVE.
 *
 *  - Armv8-A: +fp, +simd
 *  - Armv8.1-A: armv8-a, +crc, +lse, +rdma
 *  - Armv8.2-A: armv8.1-a
 *  - Armv8.3-A: armv8.2-a, +pauth
 *  - Armv8.4-A: armv8.3-a, +flagm, +fp16fml, +dotprod
 *  - Armv8.5-A: armv8.4-a, +sb, +ssbs, +predres
 *  - Armv8.6-A: armv8.5-a, +bf16, +i8mm
 *  - Armv8.7-A: armv8.6-a, +ls64
 *  - Armv8.8-A: armv8.7-a, +mops
 *  - Armv8.9-A: armv8.8-a
 *  - Armv9-A: armv8.5-a, +sve, +sve2
 *  - Armv9.1-A: armv9-a, +bf16, +i8mm
 *  - Armv9.2-A: armv9.1-a, +ls64
 *  - Armv9.3-A: armv9.2-a, +mops
 *  - Armv9.4-A: armv9.3-a
 *
 *  SVE has been optional since Armv8.2-A, but it's a requirement for Armv9.0-A.
 *  A 512-bit SVE variant has already been implemented on the Fugaku supercomputer.
 *  A more flexible version, 2x256 SVE, was implemented by the AWS Graviton3 ARM processor.
 *  Here are the most important recent families of CPU cores designed by Arm:
 *
 *  - Neoverse N1: armv8.2-a, extended with Armv8.4 "dotprod" instructions.
 *    Used in AWS @b Graviton2 and Ampere @b Altra.
 *    https://developer.arm.com/Processors/Neoverse%20N1
 *  - Neoverse V1: armv8.4-a, extended with Armv8.6 bfloat/int8 "matmul" instructions.
 *    Used in AWS @b Graviton3, which also enables `sve`, `svebf16`, and `svei8mm`.
 *    https://developer.arm.com/Processors/Neoverse%20V1
 *  - Neoverse V2: armv9.0 with SVE2 and SVE bit-permutes
 *    Used in AWS @b Graviton4, NVIDIA @b Grace, Google @b Axion.
 *    https://developer.arm.com/Processors/Neoverse%20V2
 *    The N2 core is very similar to V2 and is used by Microsoft @b Cobalt.
 *    https://developer.arm.com/Processors/Neoverse%20N2
 *
 *  On Consumer side, Apple is the biggest player with mobile @b A chips and desktop @b M chips.
 *  The M1 implements Armv8.5-A, both M2 and M3 implement Armv8.6-A, and M4 is expected to have Armv9.1-A.
 */

#ifndef SIMSIMD_H
#define SIMSIMD_H

#define SIMSIMD_VERSION_MAJOR 5
#define SIMSIMD_VERSION_MINOR 4
#define SIMSIMD_VERSION_PATCH 3

/**
 *  @brief  Removes compile-time dispatching, and replaces it with runtime dispatching.
 *          So the `simsimd_dot_f32` function will invoke the most advanced backend supported by the CPU,
 *          that runs the program, rather than the most advanced backend supported by the CPU
 *          used to compile the library or the downstream application.
 */
#ifndef SIMSIMD_DYNAMIC_DISPATCH
#define SIMSIMD_DYNAMIC_DISPATCH (0) // true or false
#endif

#include "binary.h"      // Hamming, Jaccard
#include "curved.h"      // Mahalanobis, Bilinear Forms
#include "dot.h"         // Inner (dot) product, and its conjugate
#include "geospatial.h"  // Haversine and Vincenty
#include "probability.h" // Kullback-Leibler, Jensen–Shannon
#include "sparse.h"      // Intersect
#include "spatial.h"     // L2, Cosine

// On Apple Silicon, `mrs` is not allowed in user-space, so we need to use the `sysctl` API.
#if defined(SIMSIMD_DEFINED_APPLE)
#include <sys/sysctl.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 *  @brief  Enumeration of supported metric kinds.
 *          Some have aliases for convenience.
 */
typedef enum {
    simsimd_metric_unknown_k = 0, ///< Unknown metric kind

    // Classics:
    simsimd_metric_dot_k = 'i',   ///< Inner product
    simsimd_metric_inner_k = 'i', ///< Inner product alias

    simsimd_metric_vdot_k = 'v', ///< Complex inner product

    simsimd_metric_cos_k = 'c',     ///< Cosine similarity
    simsimd_metric_cosine_k = 'c',  ///< Cosine similarity alias
    simsimd_metric_angular_k = 'c', ///< Cosine similarity alias

    simsimd_metric_l2sq_k = 'e',        ///< Squared Euclidean distance
    simsimd_metric_sqeuclidean_k = 'e', ///< Squared Euclidean distance alias

    // Binary:
    simsimd_metric_hamming_k = 'h',   ///< Hamming distance
    simsimd_metric_manhattan_k = 'h', ///< Manhattan distance is same as Hamming

    simsimd_metric_jaccard_k = 'j',  ///< Jaccard coefficient
    simsimd_metric_tanimoto_k = 'j', ///< Tanimoto coefficient is same as Jaccard

    // Sets:
    simsimd_metric_intersect_k = 'x', ///< Equivalent to unnormalized Jaccard

    // Curved Spaces:
    simsimd_metric_bilinear_k = 'b',    ///< Bilinear form
    simsimd_metric_mahalanobis_k = 'm', ///< Mahalanobis distance

    // Probability:
    simsimd_metric_kl_k = 'k',               ///< Kullback-Leibler divergence
    simsimd_metric_kullback_leibler_k = 'k', ///< Kullback-Leibler divergence alias

    simsimd_metric_js_k = 's',             ///< Jensen-Shannon divergence
    simsimd_metric_jensen_shannon_k = 's', ///< Jensen-Shannon divergence alias

} simsimd_metric_kind_t;

/**
 *  @brief  Enumeration of SIMD capabilities of the target architecture.
 */
typedef enum {
    simsimd_cap_serial_k = 1,       ///< Serial (non-SIMD) capability
    simsimd_cap_any_k = 0x7FFFFFFF, ///< Mask representing any capability with `INT_MAX`

    simsimd_cap_haswell_k = 1 << 10,  ///< x86 AVX2 capability with FMA and F16C extensions
    simsimd_cap_skylake_k = 1 << 11,  ///< x86 AVX512 baseline capability
    simsimd_cap_ice_k = 1 << 12,      ///< x86 AVX512 capability with advanced integer algos
    simsimd_cap_genoa_k = 1 << 13,    ///< x86 AVX512 capability with `bf16` support
    simsimd_cap_sapphire_k = 1 << 14, ///< x86 AVX512 capability with `f16` support

    simsimd_cap_neon_k = 1 << 20,      ///< ARM NEON baseline capability
    simsimd_cap_neon_f16_k = 1 << 21,  ///< ARM NEON `f16` capability
    simsimd_cap_neon_bf16_k = 1 << 22, ///< ARM NEON `bf16` capability
    simsimd_cap_neon_i8_k = 1 << 23,   ///< ARM NEON `i8` capability
    simsimd_cap_sve_k = 1 << 24,       ///< ARM SVE baseline capability
    simsimd_cap_sve_f16_k = 1 << 25,   ///< ARM SVE `f16` capability
    simsimd_cap_sve_bf16_k = 1 << 26,  ///< ARM SVE `bf16` capability
    simsimd_cap_sve_i8_k = 1 << 27,    ///< ARM SVE `i8` capability
    simsimd_cap_sve2_k = 1 << 28,      ///< ARM SVE2 capability
    simsimd_cap_sve2p1_k = 1 << 29,    ///< ARM SVE2p1 capability

} simsimd_capability_t;

/**
 *  @brief  Enumeration of supported data types.
 *
 *  Includes complex type descriptors which in C code would use the real counterparts,
 *  but the independent flags contain metadata to be passed between programming language
 *  interfaces.
 */
typedef enum {
    simsimd_datatype_unknown_k = 0, ///< Unknown data type
    simsimd_datatype_b8_k = 1 << 1, ///< Single-bit values packed into 8-bit words

    simsimd_datatype_i8_k = 1 << 2,  ///< 8-bit signed integer
    simsimd_datatype_i16_k = 1 << 3, ///< 16-bit signed integer
    simsimd_datatype_i32_k = 1 << 4, ///< 32-bit signed integer
    simsimd_datatype_i64_k = 1 << 5, ///< 64-bit signed integer

    simsimd_datatype_u8_k = 1 << 6,  ///< 8-bit unsigned integer
    simsimd_datatype_u16_k = 1 << 7, ///< 16-bit unsigned integer
    simsimd_datatype_u32_k = 1 << 8, ///< 32-bit unsigned integer
    simsimd_datatype_u64_k = 1 << 9, ///< 64-bit unsigned integer

    simsimd_datatype_f64_k = 1 << 10,  ///< Double precision floating point
    simsimd_datatype_f32_k = 1 << 11,  ///< Single precision floating point
    simsimd_datatype_f16_k = 1 << 12,  ///< Half precision floating point
    simsimd_datatype_bf16_k = 1 << 13, ///< Brain floating point

    simsimd_datatype_f64c_k = 1 << 20,  ///< Complex double precision floating point
    simsimd_datatype_f32c_k = 1 << 21,  ///< Complex single precision floating point
    simsimd_datatype_f16c_k = 1 << 22,  ///< Complex half precision floating point
    simsimd_datatype_bf16c_k = 1 << 23, ///< Complex brain floating point
} simsimd_datatype_t;

/**
 *  @brief  Type-punned function pointer for dense vector representations and simplest similarity measures.
 *
 *  @param[in] a    Pointer to the first data array.
 *  @param[in] b    Pointer to the second data array.
 *  @param[in] n    Number of scalar words in the input arrays.
 *                  When dealing with sub-byte data types, the number of scalar words is the number of bytes.
 *  @param[out] d   Output value as a double-precision float.
 *                  In complex dot-products @b two scalars are exported for the real and imaginary parts.
 */
typedef void (*simsimd_metric_dense_punned_t)(void const* a, void const* b, simsimd_size_t n, simsimd_distance_t* d);

/**
 *  @brief  Type-punned function pointer for sparse vector representations and similarity measures.
 *
 *  @param[in] a          Pointer to the first data array, generally a sorted array of integers.
 *  @param[in] b          Pointer to the second data array, generally a sorted array of integers.
 *  @param[in] a_length   Number of scalar words in the first input array.
 *  @param[in] b_length   Number of scalar words in the second input array.
 *  @param[out] d         Output value as a double-precision float, generally without decimals.
 */
typedef void (*simsimd_metric_sparse_punned_t)(void const* a, void const* b,                     //
                                               simsimd_size_t a_length, simsimd_size_t b_length, //
                                               simsimd_distance_t* d);

/**
 *  @brief  Type-punned function pointer for curved vector spaces and similarity measures.
 *
 *  @param[in] a    Pointer to the first data array.
 *  @param[in] b    Pointer to the second data array.
 *  @param[in] c    Pointer to the metric tensor array or some covariance matrix.
 *  @param[in] n    Number of scalar words in the input arrays.
 *  @param[out] d   Output value as a double-precision float.
 */
typedef void (*simsimd_metric_curved_punned_t)(void const* a, void const* b, void const* c, //
                                               simsimd_size_t n, simsimd_distance_t* d);

/**
 *  @brief  Type-punned function pointer for a SimSIMD public interface.
 *          Can be a `simsimd_metric_dense_punned_t`, `simsimd_metric_sparse_punned_t`,
 *          or `simsimd_metric_curved_punned_t`.
 */
typedef simsimd_metric_dense_punned_t simsimd_metric_punned_t;

#if SIMSIMD_DYNAMIC_DISPATCH
SIMSIMD_DYNAMIC simsimd_capability_t simsimd_capabilities(void);
#else
SIMSIMD_PUBLIC simsimd_capability_t simsimd_capabilities(void);
#endif

#if SIMSIMD_TARGET_X86

/**
 *  @brief  Function to determine the SIMD capabilities of the current 64-bit x86 machine at @b runtime.
 *  @return A bitmask of the SIMD capabilities represented as a `simsimd_capability_t` enum value.
 */
SIMSIMD_PUBLIC simsimd_capability_t simsimd_capabilities_x86(void) {

    /// The states of 4 registers populated for a specific "cpuid" assembly call
    union four_registers_t {
        int array[4];
        struct separate_t {
            unsigned eax, ebx, ecx, edx;
        } named;
    } info1, info7, info7sub1;

#if defined(_MSC_VER)
    __cpuidex(info1.array, 1, 0);
    __cpuidex(info7.array, 7, 0);
    __cpuidex(info7sub1.array, 7, 1);
#else
    __asm__ __volatile__("cpuid"
                         : "=a"(info1.named.eax), "=b"(info1.named.ebx), "=c"(info1.named.ecx), "=d"(info1.named.edx)
                         : "a"(1), "c"(0));
    __asm__ __volatile__("cpuid"
                         : "=a"(info7.named.eax), "=b"(info7.named.ebx), "=c"(info7.named.ecx), "=d"(info7.named.edx)
                         : "a"(7), "c"(0));
    __asm__ __volatile__("cpuid"
                         : "=a"(info7sub1.named.eax), "=b"(info7sub1.named.ebx), "=c"(info7sub1.named.ecx),
                           "=d"(info7sub1.named.edx)
                         : "a"(7), "c"(1));
#endif

    // Check for AVX2 (Function ID 7, EBX register)
    // https://github.com/llvm/llvm-project/blob/50598f0ff44f3a4e75706f8c53f3380fe7faa896/clang/lib/Headers/cpuid.h#L148
    unsigned supports_avx2 = (info7.named.ebx & 0x00000020) != 0;
    // Check for F16C (Function ID 1, ECX register)
    // https://github.com/llvm/llvm-project/blob/50598f0ff44f3a4e75706f8c53f3380fe7faa896/clang/lib/Headers/cpuid.h#L107
    unsigned supports_f16c = (info1.named.ecx & 0x20000000) != 0;
    unsigned supports_fma = (info1.named.ecx & 0x00001000) != 0;
    // Check for AVX512F (Function ID 7, EBX register)
    // https://github.com/llvm/llvm-project/blob/50598f0ff44f3a4e75706f8c53f3380fe7faa896/clang/lib/Headers/cpuid.h#L155
    unsigned supports_avx512f = (info7.named.ebx & 0x00010000) != 0;
    // Check for AVX512FP16 (Function ID 7, EDX register)
    // https://github.com/llvm/llvm-project/blob/50598f0ff44f3a4e75706f8c53f3380fe7faa896/clang/lib/Headers/cpuid.h#L198C9-L198C23
    unsigned supports_avx512fp16 = (info7.named.edx & 0x00800000) != 0;
    // Check for AVX512VNNI (Function ID 7, ECX register)
    unsigned supports_avx512vnni = (info7.named.ecx & 0x00000800) != 0;
    // Check for AVX512IFMA (Function ID 7, EBX register)
    unsigned supports_avx512ifma = (info7.named.ebx & 0x00200000) != 0;
    // Check for AVX512BITALG (Function ID 7, ECX register)
    unsigned supports_avx512bitalg = (info7.named.ecx & 0x00001000) != 0;
    // Check for AVX512VBMI2 (Function ID 7, ECX register)
    unsigned supports_avx512vbmi2 = (info7.named.ecx & 0x00000040) != 0;
    // Check for AVX512VPOPCNTDQ (Function ID 7, ECX register)
    unsigned supports_avx512vpopcntdq = (info7.named.ecx & 0x00004000) != 0;
    // Check for AVX512BF16 (Function ID 7, Sub-leaf 1, EAX register)
    // https://github.com/llvm/llvm-project/blob/50598f0ff44f3a4e75706f8c53f3380fe7faa896/clang/lib/Headers/cpuid.h#L205
    unsigned supports_avx512bf16 = (info7sub1.named.eax & 0x00000020) != 0;

    // Convert specific features into CPU generations
    unsigned supports_haswell = supports_avx2 && supports_f16c && supports_fma;
    unsigned supports_skylake = supports_avx512f;
    unsigned supports_ice = supports_avx512vnni && supports_avx512ifma && supports_avx512bitalg &&
                            supports_avx512vbmi2 && supports_avx512vpopcntdq;
    unsigned supports_genoa = supports_avx512bf16;
    unsigned supports_sapphire = supports_avx512fp16;

    return (simsimd_capability_t)(                     //
        (simsimd_cap_haswell_k * supports_haswell) |   //
        (simsimd_cap_skylake_k * supports_skylake) |   //
        (simsimd_cap_ice_k * supports_ice) |           //
        (simsimd_cap_genoa_k * supports_genoa) |       //
        (simsimd_cap_sapphire_k * supports_sapphire) | //
        (simsimd_cap_serial_k));
}

#endif // SIMSIMD_TARGET_X86

#if SIMSIMD_TARGET_ARM

/*  Compiling the next section one may get: selected processor does not support system register name 'id_aa64zfr0_el1'.
 *  Suppressing assembler errors is very complicated, so when dealing with older ARM CPUs it's simpler to compile this
 *  function targeting newer ones.
 */
#pragma GCC push_options
#pragma GCC target("arch=armv8.5-a+sve")
#ifdef __clang__
#pragma clang attribute push(__attribute__((target("arch=armv8.5-a+sve"))), apply_to = function)

#endif

/**
 *  @brief  Function to determine the SIMD capabilities of the current 64-bit Arm machine at @b runtime.
 *  @return A bitmask of the SIMD capabilities represented as a `simsimd_capability_t` enum value.
 */
SIMSIMD_PUBLIC simsimd_capability_t simsimd_capabilities_arm(void) {
#if defined(SIMSIMD_DEFINED_APPLE)
    // On Apple Silicon, `mrs` is not allowed in user-space, so we need to use the `sysctl` API.
    uint32_t supports_neon = 0, supports_fp16 = 0, supports_bf16 = 0, supports_i8mm = 0;
    size_t size = sizeof(supports_neon);
    if (sysctlbyname("hw.optional.neon", &supports_neon, &size, NULL, 0) != 0)
        supports_neon = 0;
    if (sysctlbyname("hw.optional.arm.FEAT_FP16", &supports_fp16, &size, NULL, 0) != 0)
        supports_fp16 = 0;
    if (sysctlbyname("hw.optional.arm.FEAT_BF16", &supports_bf16, &size, NULL, 0) != 0)
        supports_bf16 = 0;
    if (sysctlbyname("hw.optional.arm.FEAT_I8MM", &supports_i8mm, &size, NULL, 0) != 0)
        supports_i8mm = 0;

    return (simsimd_capability_t)(                                     //
        (simsimd_cap_neon_k * (supports_neon)) |                       //
        (simsimd_cap_neon_f16_k * (supports_neon && supports_fp16)) |  //
        (simsimd_cap_neon_bf16_k * (supports_neon && supports_bf16)) | //
        (simsimd_cap_neon_i8_k * (supports_neon && supports_i8mm)) |   //
        (simsimd_cap_serial_k));

#elif defined(SIMSIMD_DEFINED_LINUX)
    // This is how the `arm-cpusysregs` library does it:
    //
    //    int ID_AA64ISAR1_EL1_BF16() const { return (int)(_aa64isar1 >> 44) & 0x0F; }
    //    int ID_AA64ZFR0_EL1_BF16() const { return (int)(_aa64zfr0 >> 20) & 0x0F; }
    //    int ID_AA64PFR0_EL1_FP() const { return (int)(_aa64pfr0 >> 16) & 0x0F; }
    //    int ID_AA64ISAR0_EL1_DP() const { return (int)(_aa64isar0 >> 44) & 0x0F; }
    //    int ID_AA64PFR0_EL1_SVE() const { return (int)(_aa64pfr0 >> 32) & 0x0F; }
    //    int ID_AA64ZFR0_EL1_SVEver() const { return (int)(_aa64zfr0) & 0x0F; }
    //    bool FEAT_BF16() const { return ID_AA64ISAR1_EL1_BF16() >= 1 || ID_AA64ZFR0_EL1_BF16() >= 1; }
    //    bool FEAT_FP16() const { return ID_AA64PFR0_EL1_FP() >= 1 && ID_AA64PFR0_EL1_FP() < 15; }
    //    bool FEAT_DotProd() const { return ID_AA64ISAR0_EL1_DP() >= 1; }
    //    bool FEAT_SVE() const { return ID_AA64PFR0_EL1_SVE() >= 1; }
    //    bool FEAT_SVE2() const { return ID_AA64ZFR0_EL1_SVEver() >= 1; }
    //    bool FEAT_I8MM() const { return ID_AA64ZFR0_EL1_I8MM() >= 1; }
    //
    // https://github.com/lelegard/arm-cpusysregs/tree/4837c62e619a5e5f12bf41b16a1ee1e71d62c76d

    // Read CPUID registers directly
    unsigned long id_aa64isar0_el1 = 0, id_aa64isar1_el1 = 0, id_aa64pfr0_el1 = 0, id_aa64zfr0_el1 = 0;

    // Now let's unpack the status flags from ID_AA64ISAR0_EL1
    // https://developer.arm.com/documentation/ddi0601/2024-03/AArch64-Registers/ID-AA64ISAR0-EL1--AArch64-Instruction-Set-Attribute-Register-0?lang=en
    __asm__ __volatile__("mrs %0, ID_AA64ISAR0_EL1" : "=r"(id_aa64isar0_el1));
    // DP, bits [47:44] of ID_AA64ISAR0_EL1
    unsigned supports_integer_dot_products = ((id_aa64isar0_el1 >> 44) & 0xF) >= 1;
    // Now let's unpack the status flags from ID_AA64ISAR1_EL1
    // https://developer.arm.com/documentation/ddi0601/2024-03/AArch64-Registers/ID-AA64ISAR1-EL1--AArch64-Instruction-Set-Attribute-Register-1?lang=en
    __asm__ __volatile__("mrs %0, ID_AA64ISAR1_EL1" : "=r"(id_aa64isar1_el1));
    // I8MM, bits [55:52] of ID_AA64ISAR1_EL1
    unsigned supports_i8mm = ((id_aa64isar1_el1 >> 52) & 0xF) >= 1;
    // BF16, bits [47:44] of ID_AA64ISAR1_EL1
    unsigned supports_bf16 = ((id_aa64isar1_el1 >> 44) & 0xF) >= 1;

    // Now let's unpack the status flags from ID_AA64PFR0_EL1
    // https://developer.arm.com/documentation/ddi0601/2024-03/AArch64-Registers/ID-AA64PFR0-EL1--AArch64-Processor-Feature-Register-0?lang=en
    __asm__ __volatile__("mrs %0, ID_AA64PFR0_EL1" : "=r"(id_aa64pfr0_el1));
    // SVE, bits [35:32] of ID_AA64PFR0_EL1
    unsigned supports_sve = ((id_aa64pfr0_el1 >> 32) & 0xF) >= 1;
    // AdvSIMD, bits [23:20] of ID_AA64PFR0_EL1 can be used to check for `fp16` support
    //  - 0b0000: integers, single, double precision arithmetic
    //  - 0b0001: includes support for half-precision floating-point arithmetic
    unsigned supports_fp16 = ((id_aa64pfr0_el1 >> 20) & 0xF) == 1;

    // Now let's unpack the status flags from ID_AA64ZFR0_EL1
    // https://developer.arm.com/documentation/ddi0601/2024-03/AArch64-Registers/ID-AA64ZFR0-EL1--SVE-Feature-ID-Register-0?lang=en
    if (supports_sve)
        __asm__ __volatile__("mrs %0, ID_AA64ZFR0_EL1" : "=r"(id_aa64zfr0_el1));
    // I8MM, bits [47:44] of ID_AA64ZFR0_EL1
    unsigned supports_sve_i8mm = ((id_aa64zfr0_el1 >> 44) & 0xF) >= 1;
    // BF16, bits [23:20] of ID_AA64ZFR0_EL1
    unsigned supports_sve_bf16 = ((id_aa64zfr0_el1 >> 20) & 0xF) >= 1;
    // SVEver, bits [3:0] can be used to check for capability levels:
    //  - 0b0000: SVE is implemented
    //  - 0b0001: SVE2 is implemented
    //  - 0b0010: SVE2.1 is implemented
    // This value must match the existing indicator obtained from ID_AA64PFR0_EL1:
    unsigned supports_sve2 = ((id_aa64zfr0_el1) & 0xF) >= 1;
    unsigned supports_sve2p1 = ((id_aa64zfr0_el1) & 0xF) >= 2;
    unsigned supports_neon = 1; // NEON is always supported

    return (simsimd_capability_t)(                                                                    //
        (simsimd_cap_neon_k * (supports_neon)) |                                                      //
        (simsimd_cap_neon_f16_k * (supports_neon && supports_fp16)) |                                 //
        (simsimd_cap_neon_bf16_k * (supports_neon && supports_bf16)) |                                //
        (simsimd_cap_neon_i8_k * (supports_neon && supports_i8mm && supports_integer_dot_products)) | //
        (simsimd_cap_sve_k * (supports_sve)) |                                                        //
        (simsimd_cap_sve_f16_k * (supports_sve && supports_fp16)) |                                   //
        (simsimd_cap_sve_bf16_k * (supports_sve && supports_sve_bf16)) |                              //
        (simsimd_cap_sve_i8_k * (supports_sve && supports_sve_i8mm)) |                                //
        (simsimd_cap_sve2_k * (supports_sve2)) |                                                      //
        (simsimd_cap_sve2p1_k * (supports_sve2p1)) |                                                  //
        (simsimd_cap_serial_k));
#else // SIMSIMD_DEFINED_LINUX
    return simsimd_cap_serial_k;
#endif
}

#ifdef __clang__
#pragma clang attribute pop
#endif
#pragma GCC pop_options

#endif

/**
 *  @brief  Function to determine the SIMD capabilities of the current 64-bit x86 machine at @b runtime.
 *  @return A bitmask of the SIMD capabilities represented as a `simsimd_capability_t` enum value.
 */
SIMSIMD_PUBLIC simsimd_capability_t simsimd_capabilities_implementation(void) {
#if SIMSIMD_TARGET_X86
    return simsimd_capabilities_x86();
#endif // SIMSIMD_TARGET_X86
#if SIMSIMD_TARGET_ARM
    return simsimd_capabilities_arm();
#endif // SIMSIMD_TARGET_ARM
    return simsimd_cap_serial_k;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type"

#endif

/**
 *  @brief  Determines the best suited metric implementation based on the given datatype,
 *          supported and allowed by hardware capabilities.
 *
 *  @param kind The kind of metric to be evaluated.
 *  @param datatype The data type for which the metric needs to be evaluated.
 *  @param supported The hardware capabilities supported by the CPU.
 *  @param allowed The hardware capabilities allowed for use.
 *  @param metric_output Output variable for the selected similarity function.
 *  @param capability_output Output variable for the utilized hardware capabilities.
 */
SIMSIMD_PUBLIC void simsimd_find_metric_punned( //
    simsimd_metric_kind_t kind,                 //
    simsimd_datatype_t datatype,                //
    simsimd_capability_t supported,             //
    simsimd_capability_t allowed,               //
    simsimd_metric_punned_t* metric_output,     //
    simsimd_capability_t* capability_output) {

    // Modern compilers abso-freaking-lutely love optimizing-out my logic!
    // Just marking the variables as `volatile` is not enough, so we have
    // to add inline assembly to further discourage them!
#if defined(_MSC_VER)
    _ReadWriteBarrier();
#else
    __asm__ __volatile__("" ::: "memory");
#endif

    volatile simsimd_metric_punned_t* m = metric_output;
    volatile simsimd_capability_t* c = capability_output;
    volatile simsimd_capability_t viable = (simsimd_capability_t)(supported & allowed);
    *m = (simsimd_metric_punned_t)0;
    *c = (simsimd_capability_t)0;

    typedef simsimd_metric_punned_t m_t;
    switch (datatype) {

    case simsimd_datatype_unknown_k: break;

    // These data-types are not supported yet
    case simsimd_datatype_i16_k: break;
    case simsimd_datatype_i32_k: break;
    case simsimd_datatype_i64_k: break;
    case simsimd_datatype_u8_k: break;
    case simsimd_datatype_u64_k: break;

    // Double-precision floating-point vectors
    case simsimd_datatype_f64_k: {
#if SIMSIMD_TARGET_SVE
        if (viable & simsimd_cap_sve_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f64_sve, *c = simsimd_cap_sve_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_f64_sve, *c = simsimd_cap_sve_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_f64_sve, *c = simsimd_cap_sve_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_SKYLAKE
        if (viable & simsimd_cap_skylake_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f64_skylake, *c = simsimd_cap_skylake_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_f64_skylake, *c = simsimd_cap_skylake_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_f64_skylake, *c = simsimd_cap_skylake_k; return;
            default: break;
            }
#endif
        if (viable & simsimd_cap_serial_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f64_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_f64_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_f64_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_js_k: *m = (m_t)&simsimd_js_f64_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_kl_k: *m = (m_t)&simsimd_kl_f64_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_bilinear_k: *m = (m_t)&simsimd_bilinear_f64_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_mahalanobis_k:
                *m = (m_t)&simsimd_mahalanobis_f64_serial, *c = simsimd_cap_serial_k;
                return;
            default: break;
            }

        break;
    }

    // Single-precision floating-point vectors
    case simsimd_datatype_f32_k: {

#if SIMSIMD_TARGET_SVE
        if (viable & simsimd_cap_sve_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f32_sve, *c = simsimd_cap_sve_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_f32_sve, *c = simsimd_cap_sve_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_f32_sve, *c = simsimd_cap_sve_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_NEON
        if (viable & simsimd_cap_neon_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f32_neon, *c = simsimd_cap_neon_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_f32_neon, *c = simsimd_cap_neon_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_f32_neon, *c = simsimd_cap_neon_k; return;
            case simsimd_metric_js_k: *m = (m_t)&simsimd_js_f32_neon, *c = simsimd_cap_neon_k; return;
            case simsimd_metric_kl_k: *m = (m_t)&simsimd_kl_f32_neon, *c = simsimd_cap_neon_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_SKYLAKE
        if (viable & simsimd_cap_skylake_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f32_skylake, *c = simsimd_cap_skylake_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_f32_skylake, *c = simsimd_cap_skylake_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_f32_skylake, *c = simsimd_cap_skylake_k; return;
            case simsimd_metric_js_k: *m = (m_t)&simsimd_js_f32_skylake, *c = simsimd_cap_skylake_k; return;
            case simsimd_metric_kl_k: *m = (m_t)&simsimd_kl_f32_skylake, *c = simsimd_cap_skylake_k; return;
            case simsimd_metric_bilinear_k: *m = (m_t)&simsimd_bilinear_f32_skylake, *c = simsimd_cap_skylake_k; return;
            case simsimd_metric_mahalanobis_k:
                *m = (m_t)&simsimd_mahalanobis_f32_skylake, *c = simsimd_cap_skylake_k;
                return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_HASWELL
        if (viable & simsimd_cap_haswell_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f32_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_f32_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_f32_haswell, *c = simsimd_cap_haswell_k; return;
            default: break;
            }
#endif

        if (viable & simsimd_cap_serial_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f32_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_f32_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_f32_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_js_k: *m = (m_t)&simsimd_js_f32_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_kl_k: *m = (m_t)&simsimd_kl_f32_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_bilinear_k: *m = (m_t)&simsimd_bilinear_f32_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_mahalanobis_k:
                *m = (m_t)&simsimd_mahalanobis_f32_serial, *c = simsimd_cap_serial_k;
                return;
            default: break;
            }

        break;
    }
    // Half-precision floating-point vectors
    case simsimd_datatype_f16_k: {

#if SIMSIMD_TARGET_SVE_F16
        if (viable & simsimd_cap_sve_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f16_sve, *c = simsimd_cap_sve_f16_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_f16_sve, *c = simsimd_cap_sve_f16_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_f16_sve, *c = simsimd_cap_sve_f16_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_NEON_F16
        if (viable & simsimd_cap_neon_f16_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f16_neon, *c = simsimd_cap_neon_f16_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_f16_neon, *c = simsimd_cap_neon_f16_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_f16_neon, *c = simsimd_cap_neon_f16_k; return;
            case simsimd_metric_js_k: *m = (m_t)&simsimd_js_f16_neon, *c = simsimd_cap_neon_f16_k; return;
            case simsimd_metric_kl_k: *m = (m_t)&simsimd_kl_f16_neon, *c = simsimd_cap_neon_f16_k; return;
            case simsimd_metric_bilinear_k: *m = (m_t)&simsimd_bilinear_f16_neon, *c = simsimd_cap_neon_f16_k; return;
            case simsimd_metric_mahalanobis_k:
                *m = (m_t)&simsimd_mahalanobis_f16_neon, *c = simsimd_cap_neon_f16_k;
                return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_SAPPHIRE
        if (viable & simsimd_cap_sapphire_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f16_sapphire, *c = simsimd_cap_sapphire_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_f16_sapphire, *c = simsimd_cap_sapphire_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_f16_sapphire, *c = simsimd_cap_sapphire_k; return;
            case simsimd_metric_js_k: *m = (m_t)&simsimd_js_f16_sapphire, *c = simsimd_cap_sapphire_k; return;
            case simsimd_metric_kl_k: *m = (m_t)&simsimd_kl_f16_sapphire, *c = simsimd_cap_sapphire_k; return;
            case simsimd_metric_bilinear_k:
                *m = (m_t)&simsimd_bilinear_f16_sapphire, *c = simsimd_cap_sapphire_k;
                return;
            case simsimd_metric_mahalanobis_k:
                *m = (m_t)&simsimd_mahalanobis_f16_sapphire, *c = simsimd_cap_sapphire_k;
                return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_HASWELL
        if (viable & simsimd_cap_haswell_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f16_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_f16_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_f16_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_js_k: *m = (m_t)&simsimd_js_f16_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_kl_k: *m = (m_t)&simsimd_kl_f16_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_bilinear_k: *m = (m_t)&simsimd_bilinear_f16_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_mahalanobis_k:
                *m = (m_t)&simsimd_mahalanobis_f16_haswell, *c = simsimd_cap_haswell_k;
                return;
            default: break;
            }
#endif

        if (viable & simsimd_cap_serial_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f16_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_f16_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_f16_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_js_k: *m = (m_t)&simsimd_js_f16_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_kl_k: *m = (m_t)&simsimd_kl_f16_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_bilinear_k: *m = (m_t)&simsimd_bilinear_f16_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_mahalanobis_k:
                *m = (m_t)&simsimd_mahalanobis_f16_serial, *c = simsimd_cap_serial_k;
                return;
            default: break;
            }

        break;
    }
    // Brain floating-point vectors
    case simsimd_datatype_bf16_k: {
#if SIMSIMD_TARGET_SVE_BF16
        if (viable & simsimd_cap_sve_bf16_k)
            switch (kind) {
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_bf16_sve, *c = simsimd_cap_sve_bf16_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_bf16_sve, *c = simsimd_cap_sve_bf16_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_NEON_BF16
        if (viable & simsimd_cap_neon_bf16_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_bf16_neon, *c = simsimd_cap_neon_bf16_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_bf16_neon, *c = simsimd_cap_neon_bf16_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_bf16_neon, *c = simsimd_cap_neon_bf16_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_GENOA
        if (viable & simsimd_cap_genoa_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_bf16_genoa, *c = simsimd_cap_genoa_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_bf16_genoa, *c = simsimd_cap_genoa_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_bf16_genoa, *c = simsimd_cap_genoa_k; return;
            case simsimd_metric_bilinear_k: *m = (m_t)&simsimd_bilinear_bf16_genoa, *c = simsimd_cap_genoa_k; return;
            case simsimd_metric_mahalanobis_k:
                *m = (m_t)&simsimd_mahalanobis_bf16_genoa, *c = simsimd_cap_genoa_k;
                return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_HASWELL
        if (viable & simsimd_cap_haswell_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_bf16_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_bf16_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_bf16_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_bilinear_k:
                *m = (m_t)&simsimd_bilinear_bf16_haswell, *c = simsimd_cap_haswell_k;
                return;
            case simsimd_metric_mahalanobis_k:
                *m = (m_t)&simsimd_mahalanobis_bf16_haswell, *c = simsimd_cap_haswell_k;
                return;
            default: break;
            }
#endif
        if (viable & simsimd_cap_serial_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_bf16_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_bf16_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_bf16_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_js_k: *m = (m_t)&simsimd_js_bf16_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_kl_k: *m = (m_t)&simsimd_kl_bf16_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_bilinear_k: *m = (m_t)&simsimd_bilinear_bf16_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_mahalanobis_k:
                *m = (m_t)&simsimd_mahalanobis_bf16_serial, *c = simsimd_cap_serial_k;
                return;
            default: break;
            }

        break;
    }
    // Single-byte integer vectors
    case simsimd_datatype_i8_k: {
#if SIMSIMD_TARGET_NEON_I8
        if (viable & simsimd_cap_neon_i8_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_i8_neon, *c = simsimd_cap_neon_i8_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_i8_neon, *c = simsimd_cap_neon_i8_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_i8_neon, *c = simsimd_cap_neon_i8_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_ICE
        if (viable & simsimd_cap_ice_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_i8_ice, *c = simsimd_cap_ice_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_i8_ice, *c = simsimd_cap_ice_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_i8_ice, *c = simsimd_cap_ice_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_HASWELL
        if (viable & simsimd_cap_haswell_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_i8_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_i8_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_i8_haswell, *c = simsimd_cap_haswell_k; return;
            default: break;
            }
#endif

        if (viable & simsimd_cap_serial_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_i8_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_cos_k: *m = (m_t)&simsimd_cos_i8_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_l2sq_k: *m = (m_t)&simsimd_l2sq_i8_serial, *c = simsimd_cap_serial_k; return;
            default: break;
            }

        break;
    }
    // Binary vectors
    case simsimd_datatype_b8_k: {

#if SIMSIMD_TARGET_SVE
        if (viable & simsimd_cap_sve_k)
            switch (kind) {
            case simsimd_metric_hamming_k: *m = (m_t)&simsimd_hamming_b8_sve, *c = simsimd_cap_sve_k; return;
            case simsimd_metric_jaccard_k: *m = (m_t)&simsimd_jaccard_b8_sve, *c = simsimd_cap_sve_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_NEON
        if (viable & simsimd_cap_neon_k)
            switch (kind) {
            case simsimd_metric_hamming_k: *m = (m_t)&simsimd_hamming_b8_neon, *c = simsimd_cap_neon_k; return;
            case simsimd_metric_jaccard_k: *m = (m_t)&simsimd_jaccard_b8_neon, *c = simsimd_cap_neon_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_ICE
        if (viable & simsimd_cap_ice_k)
            switch (kind) {
            case simsimd_metric_hamming_k: *m = (m_t)&simsimd_hamming_b8_ice, *c = simsimd_cap_ice_k; return;
            case simsimd_metric_jaccard_k: *m = (m_t)&simsimd_jaccard_b8_ice, *c = simsimd_cap_ice_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_HASWELL
        if (viable & simsimd_cap_haswell_k)
            switch (kind) {
            case simsimd_metric_hamming_k: *m = (m_t)&simsimd_hamming_b8_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_jaccard_k: *m = (m_t)&simsimd_jaccard_b8_haswell, *c = simsimd_cap_haswell_k; return;
            default: break;
            }
#endif

        if (viable & simsimd_cap_serial_k)
            switch (kind) {
            case simsimd_metric_hamming_k: *m = (m_t)&simsimd_hamming_b8_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_jaccard_k: *m = (m_t)&simsimd_jaccard_b8_serial, *c = simsimd_cap_serial_k; return;
            default: break;
            }

        break;
    }
    // Complex floating-point vectors
    case simsimd_datatype_f32c_k: {

#if SIMSIMD_TARGET_SVE
        if (viable & simsimd_cap_sve_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f32c_sve, *c = simsimd_cap_sve_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_f32c_sve, *c = simsimd_cap_sve_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_NEON
        if (viable & simsimd_cap_neon_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f32c_neon, *c = simsimd_cap_neon_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_f32c_neon, *c = simsimd_cap_neon_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_SKYLAKE
        if (viable & simsimd_cap_skylake_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f32c_skylake, *c = simsimd_cap_skylake_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_f32c_skylake, *c = simsimd_cap_skylake_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_HASWELL
        if (viable & simsimd_cap_haswell_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f32c_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_f32c_haswell, *c = simsimd_cap_haswell_k; return;
            default: break;
            }
#endif

        if (viable & simsimd_cap_serial_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f32c_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_f32c_serial, *c = simsimd_cap_serial_k; return;
            default: break;
            }

        break;
    }
    // Complex double-precision floating-point vectors
    case simsimd_datatype_f64c_k: {

#if SIMSIMD_TARGET_SVE
        if (viable & simsimd_cap_sve_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f64c_sve, *c = simsimd_cap_sve_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_f64c_sve, *c = simsimd_cap_sve_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_SKYLAKE
        if (viable & simsimd_cap_skylake_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f64c_skylake, *c = simsimd_cap_skylake_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_f64c_skylake, *c = simsimd_cap_skylake_k; return;
            default: break;
            }
#endif

        if (viable & simsimd_cap_serial_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f64c_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_f64c_serial, *c = simsimd_cap_serial_k; return;
            default: break;
            }

        break;
    }
    // Complex half-precision floating-point vectors
    case simsimd_datatype_f16c_k: {

#if SIMSIMD_TARGET_SVE_F16
        if (viable & simsimd_cap_sve_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f16c_sve, *c = simsimd_cap_sve_f16_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_f16c_sve, *c = simsimd_cap_sve_f16_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_NEON_F16
        if (viable & simsimd_cap_neon_f16_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f16c_neon, *c = simsimd_cap_neon_f16_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_f16c_neon, *c = simsimd_cap_neon_f16_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_SAPPHIRE
        if (viable & simsimd_cap_sapphire_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f16c_sapphire, *c = simsimd_cap_sapphire_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_f16c_sapphire, *c = simsimd_cap_sapphire_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_HASWELL
        if (viable & simsimd_cap_haswell_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f16c_haswell, *c = simsimd_cap_haswell_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_f16c_haswell, *c = simsimd_cap_haswell_k; return;
            default: break;
            }
#endif

        if (viable & simsimd_cap_serial_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_f16c_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_f16c_serial, *c = simsimd_cap_serial_k; return;
            default: break;
            }

        break;
    }
    // Complex Brain floating-point vectors
    case simsimd_datatype_bf16c_k: {

#if SIMSIMD_TARGET_NEON_BF16
        if (viable & simsimd_cap_neon_bf16_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_bf16c_neon, *c = simsimd_cap_neon_bf16_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_bf16c_neon, *c = simsimd_cap_neon_bf16_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_GENOA
        if (viable & simsimd_cap_genoa_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_bf16c_genoa, *c = simsimd_cap_genoa_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_bf16c_genoa, *c = simsimd_cap_genoa_k; return;
            default: break;
            }
#endif

        if (viable & simsimd_cap_serial_k)
            switch (kind) {
            case simsimd_metric_dot_k: *m = (m_t)&simsimd_dot_bf16c_serial, *c = simsimd_cap_serial_k; return;
            case simsimd_metric_vdot_k: *m = (m_t)&simsimd_vdot_bf16c_serial, *c = simsimd_cap_serial_k; return;
            default: break;
            }

        break;
    }

    // Unsigned 16-bit integer vectors
    case simsimd_datatype_u16_k: {

#if SIMSIMD_TARGET_SVE2
        if (viable & simsimd_cap_sve_k)
            switch (kind) {
            case simsimd_metric_intersect_k: *m = (m_t)&simsimd_intersect_u16_sve2, *c = simsimd_cap_sve_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_NEON
        if (viable & simsimd_cap_neon_k)
            switch (kind) {
            case simsimd_metric_intersect_k: *m = (m_t)&simsimd_intersect_u16_neon, *c = simsimd_cap_neon_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_ICE
        if (viable & simsimd_cap_ice_k)
            switch (kind) {
            case simsimd_metric_intersect_k: *m = (m_t)&simsimd_intersect_u16_ice, *c = simsimd_cap_skylake_k; return;
            default: break;
            }
#endif
        if (viable & simsimd_cap_serial_k)
            switch (kind) {
            case simsimd_metric_intersect_k: *m = (m_t)&simsimd_intersect_u16_serial, *c = simsimd_cap_serial_k; return;
            default: break;
            }

        break;
    }
    // Unsigned 32-bit integer vectors
    case simsimd_datatype_u32_k: {

#if SIMSIMD_TARGET_SVE2
        if (viable & simsimd_cap_sve_k)
            switch (kind) {
            case simsimd_metric_intersect_k: *m = (m_t)&simsimd_intersect_u32_sve2, *c = simsimd_cap_sve_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_NEON
        if (viable & simsimd_cap_neon_k)
            switch (kind) {
            case simsimd_metric_intersect_k: *m = (m_t)&simsimd_intersect_u32_neon, *c = simsimd_cap_neon_k; return;
            default: break;
            }
#endif
#if SIMSIMD_TARGET_ICE
        if (viable & simsimd_cap_ice_k)
            switch (kind) {
            case simsimd_metric_intersect_k: *m = (m_t)&simsimd_intersect_u32_ice, *c = simsimd_cap_skylake_k; return;
            default: break;
            }
#endif
        if (viable & simsimd_cap_serial_k)
            switch (kind) {
            case simsimd_metric_intersect_k: *m = (m_t)&simsimd_intersect_u32_serial, *c = simsimd_cap_serial_k; return;
            default: break;
            }

        break;
    }
    }

    // Modern compilers abso-freaking-lutely love optimizing-out my logic!
    // Just marking the variables as `volatile` is not enough, so we have
    // to add inline assembly to further discourage them!
#if defined(_MSC_VER)
    _ReadWriteBarrier();
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

#pragma GCC diagnostic pop
#ifdef __clang__
#pragma clang diagnostic pop

#endif

/**
 *  @brief  Selects the most suitable metric implementation based on the given metric kind, datatype,
 *          and allowed capabilities. @b Don't call too often and prefer caching the `simsimd_capabilities()`.
 *
 *  @param kind The kind of metric to be evaluated.
 *  @param datatype The data type for which the metric needs to be evaluated.
 *  @param allowed The hardware capabilities allowed for use.
 *  @return A function pointer to the selected metric implementation.
 */
SIMSIMD_PUBLIC simsimd_metric_punned_t simsimd_metric_punned( //
    simsimd_metric_kind_t kind,                               //
    simsimd_datatype_t datatype,                              //
    simsimd_capability_t allowed) {

    simsimd_metric_punned_t result = 0;
    simsimd_capability_t c = simsimd_cap_serial_k;
    simsimd_capability_t supported = simsimd_capabilities();
    simsimd_find_metric_punned(kind, datatype, supported, allowed, &result, &c);
    return result;
}

#if SIMSIMD_DYNAMIC_DISPATCH

/*  Run-time feature-testing functions
 *  - Check if the CPU supports NEON or SVE extensions on Arm
 *  - Check if the CPU supports AVX2 and F16C extensions on Haswell x86 CPUs and newer
 *  - Check if the CPU supports AVX512F and AVX512BW extensions on Skylake x86 CPUs and newer
 *  - Check if the CPU supports AVX512VNNI, AVX512IFMA, AVX512BITALG, AVX512VBMI2, and AVX512VPOPCNTDQ
 *    extensions on Ice Lake x86 CPUs and newer
 *  - Check if the CPU supports AVX512FP16 extensions on Sapphire Rapids x86 CPUs and newer
 *
 *  @return 1 if the CPU supports the SIMD instruction set, 0 otherwise.
 */
SIMSIMD_DYNAMIC int simsimd_uses_neon(void);
SIMSIMD_DYNAMIC int simsimd_uses_neon_f16(void);
SIMSIMD_DYNAMIC int simsimd_uses_neon_bf16(void);
SIMSIMD_DYNAMIC int simsimd_uses_neon_i8(void);
SIMSIMD_DYNAMIC int simsimd_uses_sve(void);
SIMSIMD_DYNAMIC int simsimd_uses_sve_f16(void);
SIMSIMD_DYNAMIC int simsimd_uses_sve_bf16(void);
SIMSIMD_DYNAMIC int simsimd_uses_sve_i8(void);
SIMSIMD_DYNAMIC int simsimd_uses_sve2(void);
SIMSIMD_DYNAMIC int simsimd_uses_haswell(void);
SIMSIMD_DYNAMIC int simsimd_uses_skylake(void);
SIMSIMD_DYNAMIC int simsimd_uses_ice(void);
SIMSIMD_DYNAMIC int simsimd_uses_sapphire(void);
SIMSIMD_DYNAMIC int simsimd_uses_genoa(void);
SIMSIMD_DYNAMIC simsimd_capability_t simsimd_capabilities(void);

SIMSIMD_DYNAMIC int simsimd_uses_dynamic_dispatch(void);

/*  Inner products
 *  - Dot product: the sum of the products of the corresponding elements of two vectors.
 *  - Complex Dot product: dot product with a conjugate first argument.
 *  - Complex Conjugate Dot product: dot product with a conjugate first argument.
 *
 *  @param a The first vector.
 *  @param b The second vector.
 *  @param n The number of elements in the vectors. Even for complex variants (the number of scalars).
 *  @param d The output distance value.
 *
 *  @note The dot product can be negative, to use as a distance, take `1 - a * b`.
 *  @note The dot product is zero if and only if the two vectors are orthogonal.
 *  @note Defined only for floating-point and integer data types.
 */
SIMSIMD_DYNAMIC void simsimd_dot_f16(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_dot_bf16(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_size_t n,
                                      simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_dot_f32(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_dot_f64(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_dot_f16c(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_size_t n,
                                      simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_dot_bf16c(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_size_t n,
                                       simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_dot_f32c(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_size_t n,
                                      simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_dot_f64c(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_size_t n,
                                      simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_vdot_f16c(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_size_t n,
                                       simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_vdot_bf16c(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_size_t n,
                                        simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_vdot_f32c(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_size_t n,
                                       simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_vdot_f64c(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_size_t n,
                                       simsimd_distance_t* d);

/*  Spatial distances
 *  - Cosine distance: the cosine of the angle between two vectors.
 *  - L2 squared distance: the squared Euclidean distance between two vectors.
 *
 *  @param a The first vector.
 *  @param b The second vector.
 *  @param n The number of elements in the vectors.
 *  @param d The output distance value.
 *
 *  @note The output distance value is non-negative.
 *  @note The output distance value is zero if and only if the two vectors are identical.
 *  @note Defined only for floating-point and integer data types.
 */
SIMSIMD_DYNAMIC void simsimd_cos_i8(simsimd_i8_t const* a, simsimd_i8_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_cos_f16(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_cos_bf16(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_size_t n,
                                      simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_cos_f32(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_cos_f64(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_l2sq_i8(simsimd_i8_t const* a, simsimd_i8_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_l2sq_f16(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_size_t n,
                                      simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_l2sq_bf16(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_size_t n,
                                       simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_l2sq_f32(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_size_t n,
                                      simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_l2sq_f64(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_size_t n,
                                      simsimd_distance_t* d);

/*  Binary distances
 *  - Hamming distance: the number of positions at which the corresponding bits are different.
 *  - Jaccard distance: ratio of bit-level matching positions (intersection) to the total number of positions (union).
 *
 *  @param a The first binary vector.
 *  @param b The second binary vector.
 *  @param n The number of 8-bit words in the vectors.
 *  @param d The output distance value.
 *
 *  @note The output distance value is non-negative.
 *  @note The output distance value is zero if and only if the two vectors are identical.
 *  @note Defined only for binary data.
 */
SIMSIMD_DYNAMIC void simsimd_hamming_b8(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n,
                                        simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_jaccard_b8(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n,
                                        simsimd_distance_t* d);

/*  Probability distributions
 *  - Jensen-Shannon divergence: a measure of similarity between two probability distributions.
 *  - Kullback-Leibler divergence: a measure of how one probability distribution diverges from a second.
 *
 *  @param a The first discrete probability distribution.
 *  @param b The second discrete probability distribution.
 *  @param n The number of elements in the discrete distributions.
 *  @param d The output divergence value.
 *
 *  @note The distributions are assumed to be normalized.
 *  @note The output divergence value is non-negative.
 *  @note The output divergence value is zero if and only if the two distributions are identical.
 *  @note Defined only for floating-point data types.
 */
SIMSIMD_DYNAMIC void simsimd_kl_f16(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_kl_bf16(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_kl_f32(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_kl_f64(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_js_f16(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_js_bf16(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_js_f32(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d);
SIMSIMD_DYNAMIC void simsimd_js_f64(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d);

#else

/*  Compile-time feature-testing functions
 *  - Check if the CPU supports NEON or SVE extensions on Arm
 *  - Check if the CPU supports AVX2 and F16C extensions on Haswell x86 CPUs and newer
 *  - Check if the CPU supports AVX512F and AVX512BW extensions on Skylake x86 CPUs and newer
 *  - Check if the CPU supports AVX512VNNI, AVX512IFMA, AVX512BITALG, AVX512VBMI2, and AVX512VPOPCNTDQ
 *    extensions on Ice Lake x86 CPUs and newer
 *  - Check if the CPU supports AVX512BF16 extensions on Genoa x86 CPUs and newer
 *  - Check if the CPU supports AVX512FP16 extensions on Sapphire Rapids x86 CPUs and newer
 *
 *  @return 1 if the CPU supports the SIMD instruction set, 0 otherwise.
 */

// clang-format off
SIMSIMD_PUBLIC int simsimd_uses_neon(void) { return SIMSIMD_TARGET_ARM && SIMSIMD_TARGET_NEON; }
SIMSIMD_PUBLIC int simsimd_uses_neon_f16(void) { return SIMSIMD_TARGET_ARM && SIMSIMD_TARGET_NEON && SIMSIMD_NATIVE_F16; }
SIMSIMD_PUBLIC int simsimd_uses_neon_bf16(void) { return SIMSIMD_TARGET_ARM && SIMSIMD_TARGET_NEON && SIMSIMD_NATIVE_BF16; }
SIMSIMD_PUBLIC int simsimd_uses_neon_i8(void) { return SIMSIMD_TARGET_ARM && SIMSIMD_TARGET_NEON; }
SIMSIMD_PUBLIC int simsimd_uses_sve(void) { return SIMSIMD_TARGET_ARM && SIMSIMD_TARGET_SVE; }
SIMSIMD_PUBLIC int simsimd_uses_sve_f16(void) { return SIMSIMD_TARGET_ARM && SIMSIMD_TARGET_SVE && SIMSIMD_NATIVE_F16; }
SIMSIMD_PUBLIC int simsimd_uses_sve_bf16(void) { return SIMSIMD_TARGET_ARM && SIMSIMD_TARGET_SVE && SIMSIMD_NATIVE_BF16; }
SIMSIMD_PUBLIC int simsimd_uses_sve_i8(void) { return SIMSIMD_TARGET_ARM && SIMSIMD_TARGET_SVE; }
SIMSIMD_PUBLIC int simsimd_uses_sve2(void) { return SIMSIMD_TARGET_ARM && SIMSIMD_TARGET_SVE2; }
SIMSIMD_PUBLIC int simsimd_uses_haswell(void) { return SIMSIMD_TARGET_X86 && SIMSIMD_TARGET_HASWELL; }
SIMSIMD_PUBLIC int simsimd_uses_skylake(void) { return SIMSIMD_TARGET_X86 && SIMSIMD_TARGET_SKYLAKE; }
SIMSIMD_PUBLIC int simsimd_uses_ice(void) { return SIMSIMD_TARGET_X86 && SIMSIMD_TARGET_ICE; }
SIMSIMD_PUBLIC int simsimd_uses_sapphire(void) { return SIMSIMD_TARGET_X86 && SIMSIMD_TARGET_SAPPHIRE; }
SIMSIMD_PUBLIC int simsimd_uses_genoa(void) { return SIMSIMD_TARGET_X86 && SIMSIMD_TARGET_GENOA; }
SIMSIMD_PUBLIC int simsimd_uses_dynamic_dispatch(void) { return 0; }
SIMSIMD_PUBLIC simsimd_capability_t simsimd_capabilities(void) { return simsimd_capabilities_implementation(); }
// clang-format on

/*  Inner products
 *  - Dot product: the sum of the products of the corresponding elements of two vectors.
 *  - Complex Dot product: dot product with a conjugate first argument.
 *  - Complex Conjugate Dot product: dot product with a conjugate first argument.
 *
 *  @param a The first vector.
 *  @param b The second vector.
 *  @param n The number of elements in the vectors. Even for complex variants (the number of scalars).
 *  @param d The output distance value.
 *
 *  @note The dot product can be negative, to use as a distance, take `1 - a * b`.
 *  @note The dot product is zero if and only if the two vectors are orthogonal.
 *  @note Defined only for floating-point and integer data types.
 */
SIMSIMD_PUBLIC void simsimd_dot_f16(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE_F16
    simsimd_dot_f16_sve(a, b, n, d);
#elif SIMSIMD_TARGET_NEON_F16
    simsimd_dot_f16_neon(a, b, n, d);
#elif SIMSIMD_TARGET_SAPPHIRE
    simsimd_dot_f16_sapphire(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_dot_f16_haswell(a, b, n, d);
#else
    simsimd_dot_f16_serial(a, b, n, d);
#endif
}

SIMSIMD_PUBLIC void simsimd_dot_bf16(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d) {
#if SIMSIMD_TARGET_GENOA
    simsimd_dot_bf16_genoa(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_dot_bf16_haswell(a, b, n, d);
#elif SIMSIMD_TARGET_NEON_BF16
    simsimd_dot_bf16_neon(a, b, n, d);
#else
    simsimd_dot_bf16_serial(a, b, n, d);
#endif
}

SIMSIMD_PUBLIC void simsimd_dot_f32(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE
    simsimd_dot_f32_sve(a, b, n, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_dot_f32_neon(a, b, n, d);
#elif SIMSIMD_TARGET_SKYLAKE
    simsimd_dot_f32_skylake(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_dot_f32_haswell(a, b, n, d);
#else
    simsimd_dot_f32_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_dot_f64(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE
    simsimd_dot_f64_sve(a, b, n, d);
#elif SIMSIMD_TARGET_SKYLAKE
    simsimd_dot_f64_skylake(a, b, n, d);
#else
    simsimd_dot_f64_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_dot_f16c(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE_F16
    simsimd_dot_f16c_sve(a, b, n, d);
#elif SIMSIMD_TARGET_NEON_F16
    simsimd_dot_f16c_neon(a, b, n, d);
#elif SIMSIMD_TARGET_SAPPHIRE
    simsimd_dot_f16c_sapphire(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_dot_f16c_haswell(a, b, n, d);
#else
    simsimd_dot_f16c_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_dot_bf16c(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_size_t n,
                                      simsimd_distance_t* d) {
#if SIMSIMD_TARGET_GENOA
    simsimd_dot_bf16c_genoa(a, b, n, d);
#elif SIMSIMD_TARGET_NEON_BF16
    simsimd_dot_bf16c_neon(a, b, n, d);
#else
    simsimd_dot_bf16c_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_dot_f32c(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE
    simsimd_dot_f32c_sve(a, b, n, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_dot_f32c_neon(a, b, n, d);
#elif SIMSIMD_TARGET_SKYLAKE
    simsimd_dot_f32c_skylake(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_dot_f32c_haswell(a, b, n, d);
#else
    simsimd_dot_f32c_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_dot_f64c(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE
    simsimd_dot_f64c_sve(a, b, n, d);
#elif SIMSIMD_TARGET_SKYLAKE
    simsimd_dot_f64c_skylake(a, b, n, d);
#else
    simsimd_dot_f64c_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_vdot_f16c(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_size_t n,
                                      simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE
    simsimd_vdot_f16c_sve(a, b, n, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_dot_f16c_neon(a, b, n, d);
#elif SIMSIMD_TARGET_SAPPHIRE
    simsimd_dot_f16c_sapphire(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_dot_f16c_haswell(a, b, n, d);
#else
    simsimd_vdot_f16c_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_vdot_bf16c(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_size_t n,
                                       simsimd_distance_t* d) {
#if SIMSIMD_TARGET_GENOA
    simsimd_vdot_bf16c_genoa(a, b, n, d);
#elif SIMSIMD_TARGET_NEON_BF16
    simsimd_dot_bf16c_neon(a, b, n, d);
#else
    simsimd_vdot_bf16c_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_vdot_f32c(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_size_t n,
                                      simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE
    simsimd_vdot_f32c_sve(a, b, n, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_dot_f32c_neon(a, b, n, d);
#elif SIMSIMD_TARGET_SKYLAKE
    simsimd_dot_f32c_skylake(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_dot_f32c_haswell(a, b, n, d);
#else
    simsimd_vdot_f32c_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_vdot_f64c(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_size_t n,
                                      simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE
    simsimd_vdot_f64c_sve(a, b, n, d);
#elif SIMSIMD_TARGET_SKYLAKE
    simsimd_vdot_f64c_skylake(a, b, n, d);
#else
    simsimd_vdot_f64c_serial(a, b, n, d);
#endif
}

/*  Spatial distances
 *  - Cosine distance: the cosine of the angle between two vectors.
 *  - L2 squared distance: the squared Euclidean distance between two vectors.
 *
 *  @param a The first vector.
 *  @param b The second vector.
 *  @param n The number of elements in the vectors.
 *  @param d The output distance value.
 *
 *  @note The output distance value is non-negative.
 *  @note The output distance value is zero if and only if the two vectors are identical.
 *  @note Defined only for floating-point and integer data types.
 */
SIMSIMD_PUBLIC void simsimd_cos_i8(simsimd_i8_t const* a, simsimd_i8_t const* b, simsimd_size_t n,
                                   simsimd_distance_t* d) {
#if SIMSIMD_TARGET_NEON
    simsimd_cos_i8_neon(a, b, n, d);
#elif SIMSIMD_TARGET_ICE
    simsimd_cos_i8_ice(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_cos_i8_haswell(a, b, n, d);
#else
    simsimd_cos_i8_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_l2sq_i8(simsimd_i8_t const* a, simsimd_i8_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d) {
#if SIMSIMD_TARGET_NEON
    simsimd_l2sq_i8_neon(a, b, n, d);
#elif SIMSIMD_TARGET_ICE
    simsimd_l2sq_i8_ice(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_l2sq_i8_haswell(a, b, n, d);
#else
    simsimd_l2sq_i8_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_cos_f16(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE_F16
    simsimd_cos_f16_sve(a, b, n, d);
#elif SIMSIMD_TARGET_NEON_F16
    simsimd_cos_f16_neon(a, b, n, d);
#elif SIMSIMD_TARGET_SAPPHIRE
    simsimd_cos_f16_sapphire(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_cos_f16_haswell(a, b, n, d);
#else
    simsimd_cos_f16_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_cos_bf16(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d) {
#if SIMSIMD_TARGET_GENOA
    simsimd_cos_bf16_genoa(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_cos_bf16_haswell(a, b, n, d);
#elif SIMSIMD_TARGET_SVE_BF16
    simsimd_cos_bf16_sve(a, b, n, d);
#elif SIMSIMD_TARGET_NEON_BF16
    simsimd_cos_bf16_neon(a, b, n, d);
#else
    simsimd_cos_bf16_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_cos_f32(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE
    simsimd_cos_f32_sve(a, b, n, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_cos_f32_neon(a, b, n, d);
#elif SIMSIMD_TARGET_SKYLAKE
    simsimd_cos_f32_skylake(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_cos_f32_haswell(a, b, n, d);
#else
    simsimd_cos_f32_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_cos_f64(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE
    simsimd_cos_f64_sve(a, b, n, d);
#elif SIMSIMD_TARGET_SKYLAKE
    simsimd_cos_f64_skylake(a, b, n, d);
#else
    simsimd_cos_f64_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_l2sq_f16(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE_F16
    simsimd_l2sq_f16_sve(a, b, n, d);
#elif SIMSIMD_TARGET_NEON_F16
    simsimd_l2sq_f16_neon(a, b, n, d);
#elif SIMSIMD_TARGET_SAPPHIRE
    simsimd_l2sq_f16_sapphire(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_l2sq_f16_haswell(a, b, n, d);
#else
    simsimd_l2sq_f16_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_l2sq_bf16(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_size_t n,
                                      simsimd_distance_t* d) {
#if SIMSIMD_TARGET_GENOA
    simsimd_l2sq_bf16_genoa(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_l2sq_bf16_haswell(a, b, n, d);
#elif SIMSIMD_TARGET_SVE_BF16
    simsimd_l2sq_bf16_sve(a, b, n, d);
#elif SIMSIMD_TARGET_NEON_BF16
    simsimd_l2sq_bf16_neon(a, b, n, d);
#else
    simsimd_l2sq_bf16_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_l2sq_f32(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE
    simsimd_l2sq_f32_sve(a, b, n, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_l2sq_f32_neon(a, b, n, d);
#elif SIMSIMD_TARGET_SKYLAKE
    simsimd_l2sq_f32_skylake(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_l2sq_f32_haswell(a, b, n, d);
#else
    simsimd_l2sq_f32_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_l2sq_f64(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_size_t n,
                                     simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE
    simsimd_l2sq_f64_sve(a, b, n, d);
#elif SIMSIMD_TARGET_SKYLAKE
    simsimd_l2sq_f64_skylake(a, b, n, d);
#else
    simsimd_l2sq_f64_serial(a, b, n, d);
#endif
}

/*  Binary distances
 *  - Hamming distance: the number of positions at which the corresponding bits are different.
 *  - Jaccard distance: ratio of bit-level matching positions (intersection) to the total number of positions (union).
 *
 *  @param a The first binary vector.
 *  @param b The second binary vector.
 *  @param n The number of 8-bit words in the vectors.
 *  @param d The output distance value.
 *
 *  @note The output distance value is non-negative.
 *  @note The output distance value is zero if and only if the two vectors are identical.
 *  @note Defined only for binary data.
 */
SIMSIMD_PUBLIC void simsimd_hamming_b8(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n,
                                       simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE
    simsimd_hamming_b8_sve(a, b, n, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_hamming_b8_neon(a, b, n, d);
#elif SIMSIMD_TARGET_ICE
    simsimd_hamming_b8_ice(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_hamming_b8_haswell(a, b, n, d);
#else
    simsimd_hamming_b8_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_jaccard_b8(simsimd_b8_t const* a, simsimd_b8_t const* b, simsimd_size_t n,
                                       simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE
    simsimd_jaccard_b8_sve(a, b, n, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_jaccard_b8_neon(a, b, n, d);
#elif SIMSIMD_TARGET_ICE
    simsimd_jaccard_b8_ice(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_jaccard_b8_haswell(a, b, n, d);
#else
    simsimd_jaccard_b8_serial(a, b, n, d);
#endif
}

/*  Probability distributions
 *  - Jensen-Shannon divergence: a measure of similarity between two probability distributions.
 *  - Kullback-Leibler divergence: a measure of how one probability distribution diverges from a second.
 *
 *  @param a The first discrete probability distribution.
 *  @param b The second discrete probability distribution.
 *  @param n The number of elements in the discrete distributions.
 *  @param d The output divergence value.
 *
 *  @note The distributions are assumed to be normalized.
 *  @note The output divergence value is non-negative.
 *  @note The output divergence value is zero if and only if the two distributions are identical.
 *  @note Defined only for floating-point data types.
 */
SIMSIMD_PUBLIC void simsimd_kl_f16(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_size_t n,
                                   simsimd_distance_t* d) {
#if SIMSIMD_TARGET_NEON
    simsimd_kl_f16_neon(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_kl_f16_haswell(a, b, n, d);
#else
    simsimd_kl_f16_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_kl_bf16(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d) {
    simsimd_kl_bf16_serial(a, b, n, d);
}
SIMSIMD_PUBLIC void simsimd_kl_f32(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_size_t n,
                                   simsimd_distance_t* d) {
#if SIMSIMD_TARGET_NEON
    simsimd_kl_f32_neon(a, b, n, d);
#elif SIMSIMD_TARGET_SKYLAKE
    simsimd_kl_f32_skylake(a, b, n, d);
#else
    simsimd_kl_f32_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_kl_f64(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_size_t n,
                                   simsimd_distance_t* d) {
    simsimd_kl_f64_serial(a, b, n, d);
}
SIMSIMD_PUBLIC void simsimd_js_f16(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_size_t n,
                                   simsimd_distance_t* d) {
#if SIMSIMD_TARGET_NEON
    simsimd_js_f16_neon(a, b, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_js_f16_haswell(a, b, n, d);
#else
    simsimd_js_f16_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_js_bf16(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_size_t n,
                                    simsimd_distance_t* d) {
    simsimd_js_bf16_serial(a, b, n, d);
}
SIMSIMD_PUBLIC void simsimd_js_f32(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_size_t n,
                                   simsimd_distance_t* d) {
#if SIMSIMD_TARGET_NEON
    simsimd_js_f32_neon(a, b, n, d);
#elif SIMSIMD_TARGET_SKYLAKE
    simsimd_js_f32_skylake(a, b, n, d);
#else
    simsimd_js_f32_serial(a, b, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_js_f64(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_size_t n,
                                   simsimd_distance_t* d) {
    simsimd_js_f64_serial(a, b, n, d);
}

/*  Set operations
 *
 *  @param a The first sorted array of integers.
 *  @param b The second sorted array of integers.
 *  @param a_length The number of elements in the first array.
 *  @param b_length The number of elements in the second array.
 *  @param d The output for the number of elements in the intersection.
 */
SIMSIMD_PUBLIC void simsimd_intersect_u16(simsimd_u16_t const* a, simsimd_u16_t const* b, simsimd_size_t a_length,
                                          simsimd_size_t b_length, simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE2
    simsimd_intersect_u16_sve2(a, b, a_length, b_length, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_intersect_u16_neon(a, b, a_length, b_length, d);
#elif SIMSIMD_TARGET_ICE
    simsimd_intersect_u16_ice(a, b, a_length, b_length, d);
#else
    simsimd_intersect_u16_serial(a, b, a_length, b_length, d);
#endif
}

SIMSIMD_PUBLIC void simsimd_intersect_u32(simsimd_u32_t const* a, simsimd_u32_t const* b, simsimd_size_t a_length,
                                          simsimd_size_t b_length, simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SVE2
    simsimd_intersect_u32_sve2(a, b, a_length, b_length, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_intersect_u32_neon(a, b, a_length, b_length, d);
#elif SIMSIMD_TARGET_ICE
    simsimd_intersect_u32_ice(a, b, a_length, b_length, d);
#else
    simsimd_intersect_u32_serial(a, b, a_length, b_length, d);
#endif
}

/*  Curved space distances
 *
 *  @param a The first vector of floating point values.
 *  @param b The second vector of floating point values.
 *  @param c The metric tensor or covariance matrix.
 *  @param n The number of dimensions in the vectors.
 *  @param d The output for the number of elements in the intersection.
 */
SIMSIMD_PUBLIC void simsimd_bilinear_f64(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_f64_t const* c,
                                         simsimd_size_t n, simsimd_distance_t* d) {

    simsimd_bilinear_f64_serial(a, b, c, n, d);
}
SIMSIMD_PUBLIC void simsimd_mahalanobis_f64(simsimd_f64_t const* a, simsimd_f64_t const* b, simsimd_f64_t const* c,
                                            simsimd_size_t n, simsimd_distance_t* d) {
    simsimd_mahalanobis_f64_serial(a, b, c, n, d);
}
SIMSIMD_PUBLIC void simsimd_bilinear_f32(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_f32_t const* c,
                                         simsimd_size_t n, simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SKYLAKE
    simsimd_bilinear_f32_skylake(a, b, c, n, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_bilinear_f32_neon(a, b, c, n, d);
#else
    simsimd_bilinear_f32_serial(a, b, c, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_mahalanobis_f32(simsimd_f32_t const* a, simsimd_f32_t const* b, simsimd_f32_t const* c,
                                            simsimd_size_t n, simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SKYLAKE
    simsimd_mahalanobis_f32_skylake(a, b, c, n, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_mahalanobis_f32_neon(a, b, c, n, d);
#else
    simsimd_mahalanobis_f32_serial(a, b, c, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_bilinear_f16(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_f16_t const* c,
                                         simsimd_size_t n, simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SAPPHIRE
    simsimd_bilinear_f16_sapphire(a, b, c, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_bilinear_f16_haswell(a, b, c, n, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_bilinear_f16_neon(a, b, c, n, d);
#else
    simsimd_bilinear_f16_serial(a, b, c, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_mahalanobis_f16(simsimd_f16_t const* a, simsimd_f16_t const* b, simsimd_f16_t const* c,
                                            simsimd_size_t n, simsimd_distance_t* d) {
#if SIMSIMD_TARGET_SAPPHIRE
    simsimd_mahalanobis_f16_sapphire(a, b, c, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_mahalanobis_f16_haswell(a, b, c, n, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_mahalanobis_f16_neon(a, b, c, n, d);
#else
    simsimd_mahalanobis_f16_serial(a, b, c, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_bilinear_bf16(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_bf16_t const* c,
                                          simsimd_size_t n, simsimd_distance_t* d) {
#if SIMSIMD_TARGET_GENOA
    simsimd_bilinear_bf16_genoa(a, b, c, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_bilinear_bf16_haswell(a, b, c, n, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_bilinear_bf16_neon(a, b, c, n, d);
#else
    simsimd_bilinear_bf16_serial(a, b, c, n, d);
#endif
}
SIMSIMD_PUBLIC void simsimd_mahalanobis_bf16(simsimd_bf16_t const* a, simsimd_bf16_t const* b, simsimd_bf16_t const* c,
                                             simsimd_size_t n, simsimd_distance_t* d) {
#if SIMSIMD_TARGET_GENOA
    simsimd_mahalanobis_bf16_genoa(a, b, c, n, d);
#elif SIMSIMD_TARGET_HASWELL
    simsimd_mahalanobis_bf16_haswell(a, b, c, n, d);
#elif SIMSIMD_TARGET_NEON
    simsimd_mahalanobis_bf16_neon(a, b, c, n, d);
#else
    simsimd_mahalanobis_bf16_serial(a, b, c, n, d);
#endif
}

#endif

#ifdef __cplusplus
}
#endif

#endif // SIMSIMD_H

// This file is part of the simsimd inline third-party dependency of YugabyteDB.
// Git repo: https://github.com/yugabyte/simsimd
// Git tag: v5.4.3-yb-1
//
// See also src/inline-thirdparty/README.md.
