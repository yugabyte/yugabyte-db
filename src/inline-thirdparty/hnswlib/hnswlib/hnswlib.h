#pragma once

// https://github.com/nmslib/hnswlib/pull/508
// This allows others to provide their own error stream (e.g. RcppHNSW)
#ifndef HNSWLIB_ERR_OVERRIDE
  #define HNSWERR std::cerr
#else
  #define HNSWERR HNSWLIB_ERR_OVERRIDE
#endif

#ifndef NO_MANUAL_VECTORIZATION
#if (defined(__SSE__) || _M_IX86_FP > 0 || defined(_M_AMD64) || defined(_M_X64))
#define USE_SSE
#ifdef __AVX__
#define USE_AVX
#ifdef __AVX512F__
#define USE_AVX512
#endif
#endif
#endif
#endif

#ifdef __clang__
#define PRAGMA_POP_OPTIONS _Pragma("clang attribute pop")
#define PRAGMA_PUSH_AVX_OPTIONS _Pragma("clang attribute push(__attribute__((target(\"avx\"))), apply_to = function)")
#define PRAGMA_PUSH_AVX512_OPTIONS _Pragma("clang attribute push(__attribute__((target(\"avx2,avx512f,avx512vl,bmi2\"))), apply_to = function)")
#else
#define PRAGMA_POP_OPTIONS _Pragma("GCC pop_options")
#define PRAGMA_PUSH_AVX_OPTIONS \
            _Pragma("GCC push_options") \
            _Pragma("GCC target(\"avx\")")
#define PRAGMA_PUSH_AVX512_OPTIONS \
            _Pragma("GCC push_options") \
            _Pragma("GCC target(\"avx2\", \"avx512f\", \"avx512vl\", \"bmi2\")")
#endif

#if defined(USE_AVX) || defined(USE_SSE)
#ifdef _MSC_VER
#include <intrin.h>
#include <stdexcept>
static void cpuid(int32_t out[4], int32_t eax, int32_t ecx) {
    __cpuidex(out, eax, ecx);
}
static __int64 xgetbv(unsigned int x) {
    return _xgetbv(x);
}
#else
#include <x86intrin.h>
#include <cpuid.h>
#include <stdint.h>
static void cpuid(int32_t cpuInfo[4], int32_t eax, int32_t ecx) {
    __cpuid_count(eax, ecx, cpuInfo[0], cpuInfo[1], cpuInfo[2], cpuInfo[3]);
}
static uint64_t xgetbv(unsigned int index) {
    uint32_t eax, edx;
    __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
    return ((uint64_t)edx << 32) | eax;
}
#endif

#if defined(USE_AVX512)
#include <immintrin.h>
#endif

#if defined(__GNUC__)
#define PORTABLE_ALIGN32 __attribute__((aligned(32)))
#define PORTABLE_ALIGN64 __attribute__((aligned(64)))
#else
#define PORTABLE_ALIGN32 __declspec(align(32))
#define PORTABLE_ALIGN64 __declspec(align(64))
#endif

// Adapted from https://github.com/Mysticial/FeatureDetector
#define _XCR_XFEATURE_ENABLED_MASK  0

static bool AVXCapable() {
    int cpuInfo[4];

    // CPU support
    cpuid(cpuInfo, 0, 0);
    int nIds = cpuInfo[0];

    bool HW_AVX = false;
    if (nIds >= 0x00000001) {
        cpuid(cpuInfo, 0x00000001, 0);
        HW_AVX = (cpuInfo[2] & ((int)1 << 28)) != 0;
    }

    // OS support
    cpuid(cpuInfo, 1, 0);

    bool osUsesXSAVE_XRSTORE = (cpuInfo[2] & (1 << 27)) != 0;
    bool cpuAVXSuport = (cpuInfo[2] & (1 << 28)) != 0;

    bool avxSupported = false;
    if (osUsesXSAVE_XRSTORE && cpuAVXSuport) {
        uint64_t xcrFeatureMask = xgetbv(_XCR_XFEATURE_ENABLED_MASK);
        avxSupported = (xcrFeatureMask & 0x6) == 0x6;
    }
    return HW_AVX && avxSupported;
}

static bool AVX512Capable() {
    if (!AVXCapable()) return false;

    int cpuInfo[4];

    // CPU support
    cpuid(cpuInfo, 0, 0);
    int nIds = cpuInfo[0];

    bool HW_AVX512F = false;
    if (nIds >= 0x00000007) {  //  AVX512 Foundation
        cpuid(cpuInfo, 0x00000007, 0);
        HW_AVX512F = (cpuInfo[1] & ((int)1 << 16)) != 0;
    }

    // OS support
    cpuid(cpuInfo, 1, 0);

    bool osUsesXSAVE_XRSTORE = (cpuInfo[2] & (1 << 27)) != 0;
    bool cpuAVXSuport = (cpuInfo[2] & (1 << 28)) != 0;

    bool avx512Supported = false;
    if (osUsesXSAVE_XRSTORE && cpuAVXSuport) {
        uint64_t xcrFeatureMask = xgetbv(_XCR_XFEATURE_ENABLED_MASK);
        avx512Supported = (xcrFeatureMask & 0xe6) == 0xe6;
    }
    return HW_AVX512F && avx512Supported;
}
#endif

#include <queue>
#include <vector>
#include <iostream>
#include <string.h>

namespace hnswlib {

// This can be extended to store state for filtering (e.g. from a std::set)
template<typename label_t>
class BaseFilterFunctor {
 public:
    virtual bool operator()(label_t id) { return true; }
    virtual ~BaseFilterFunctor() {};
};

template<typename dist_t, typename label_t>
class BaseSearchStopCondition {
 public:
    virtual void add_point_to_result(label_t label, const void *datapoint, dist_t dist) = 0;

    virtual void remove_point_from_result(label_t label, const void *datapoint, dist_t dist) = 0;

    virtual bool should_stop_search(dist_t candidate_dist, dist_t lowerBound) = 0;

    virtual bool should_consider_candidate(dist_t candidate_dist, dist_t lowerBound) = 0;

    virtual bool should_remove_extra() = 0;

    virtual void filter_results(std::vector<std::pair<dist_t, label_t >> &candidates) = 0;

    virtual ~BaseSearchStopCondition() {}
};

template <typename T>
class pairGreater {
 public:
    bool operator()(const T& p1, const T& p2) {
        return p1.first > p2.first;
    }
};

template<typename T>
static void writeBinaryPOD(std::ostream &out, const T &podRef) {
    out.write((char *) &podRef, sizeof(T));
}

template<typename T>
static void readBinaryPOD(std::istream &in, T &podRef) {
    in.read((char *) &podRef, sizeof(T));
}

template<typename MTYPE>
using DISTFUNC = MTYPE(*)(const void *, const void *, const void *);

template<typename MTYPE>
class SpaceInterface {
 public:
    // virtual void search(void *);
    virtual size_t get_data_size() = 0;

    virtual DISTFUNC<MTYPE> get_dist_func() = 0;

    virtual void *get_dist_func_param() = 0;

    virtual ~SpaceInterface() {}
};

template<typename dist_t, typename label_t>
class AlgorithmInterface {
 public:
    virtual void addPoint(const void *datapoint, label_t label, bool replace_deleted = false) = 0;

    virtual std::priority_queue<std::pair<dist_t, label_t>>
        searchKnn(const void*, size_t, BaseFilterFunctor<label_t>* isIdAllowed = nullptr, size_t ef = 0) const = 0;

    // Return k nearest neighbor in the order of closer fist
    virtual std::vector<std::pair<dist_t, label_t>>
        searchKnnCloserFirst(const void* query_data, size_t k, BaseFilterFunctor<label_t>* isIdAllowed = nullptr, size_t ef = 0) const;

    virtual void saveIndex(const std::string &location) = 0;

    virtual int getMaxLevel() const = 0;

    virtual ~AlgorithmInterface(){
    }
};

template<typename dist_t, typename label_t>
std::vector<std::pair<dist_t, label_t>>
AlgorithmInterface<dist_t, label_t>::searchKnnCloserFirst(
        const void* query_data, size_t k, BaseFilterFunctor<label_t>* isIdAllowed, size_t ef) const {
    std::vector<std::pair<dist_t, label_t>> result;

    // here searchKnn returns the result in the order of further first
    auto ret = searchKnn(query_data, k, isIdAllowed, ef);
    {
        size_t sz = ret.size();
        result.resize(sz);
        while (!ret.empty()) {
            result[--sz] = ret.top();
            ret.pop();
        }
    }

    return result;
}
}  // namespace hnswlib

#include "space_l2.h"
#include "space_ip.h"
#include "stop_condition.h"
#include "bruteforce.h"
#include "hnswalg.h"

// This file is part of the hnswlib inline third-party dependency of YugabyteDB.
// Git repo: https://github.com/yugabyte/hnswlib
// Git tag: vc1b9b79a-yb-1
//
// See also src/inline-thirdparty/README.md.
