#pragma once
#define __STDC_WANT_IEC_60559_TYPES_EXT__
#include <float.h>  // `_Float16`
#include <stdlib.h> // `aligned_alloc`

#include <cstring> // `std::strncmp`
#include <numeric> // `std::iota`
#include <thread>  // `std::thread`
#include <vector>  // `std::vector`

#include <atomic> // `std::atomic`
#include <thread> // `std::thread`

#include <usearch/index.hpp> // `expected_gt` and macros

#if !defined(USEARCH_USE_OPENMP)
#define USEARCH_USE_OPENMP 0
#endif

#if USEARCH_USE_OPENMP
#include <omp.h> // `omp_get_num_threads()`
#endif

#if defined(USEARCH_DEFINED_LINUX)
#include <sys/auxv.h> // `getauxval()`
#endif

#if !defined(USEARCH_USE_FP16LIB)
#if defined(__AVX512F__)
#define USEARCH_USE_FP16LIB 0
#elif defined(USEARCH_DEFINED_ARM)
#include <arm_fp16.h> // `__fp16`
#define USEARCH_USE_FP16LIB 0
#else
#define USEARCH_USE_FP16LIB 1
#endif
#endif

#if USEARCH_USE_FP16LIB
#include <fp16/fp16.h>
#endif

#if !defined(USEARCH_USE_SIMSIMD)
#define USEARCH_USE_SIMSIMD 0
#endif

#if USEARCH_USE_SIMSIMD
// Propagate the `f16` settings
#define SIMSIMD_NATIVE_F16 !USEARCH_USE_FP16LIB
#define SIMSIMD_DYNAMIC_DISPATCH 0
// No problem, if some of the functions are unused or undefined
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma warning(push)
#pragma warning(disable : 4101)
#include <simsimd/simsimd.h>
#pragma warning(pop)
#pragma GCC diagnostic pop
#endif

namespace unum {
namespace usearch {

using u40_t = uint40_t;
enum b1x8_t : unsigned char {};

struct uuid_t {
    std::uint8_t octets[16];
};

class f16_bits_t;
class i8_converted_t;

#if !USEARCH_USE_FP16LIB
#if defined(USEARCH_DEFINED_ARM)
using f16_native_t = __fp16;
#else
using f16_native_t = _Float16;
#endif
using f16_t = f16_native_t;
#else
using f16_native_t = void;
using f16_t = f16_bits_t;
#endif

using f64_t = double;
using f32_t = float;

using u64_t = std::uint64_t;
using u32_t = std::uint32_t;
using u16_t = std::uint16_t;
using u8_t = std::uint8_t;

using i64_t = std::int64_t;
using i32_t = std::int32_t;
using i16_t = std::int16_t;
using i8_t = std::int8_t;

enum class metric_kind_t : std::uint8_t {
    unknown_k = 0,
    // Classics:
    ip_k = 'i',
    cos_k = 'c',
    l2sq_k = 'e',

    // Custom:
    pearson_k = 'p',
    haversine_k = 'h',
    divergence_k = 'd',

    // Sets:
    jaccard_k = 'j',
    hamming_k = 'b',
    tanimoto_k = 't',
    sorensen_k = 's',
};

enum class scalar_kind_t : std::uint8_t {
    unknown_k = 0,
    // Custom:
    b1x8_k = 1,
    u40_k = 2,
    uuid_k = 3,
    // Common:
    f64_k = 10,
    f32_k = 11,
    f16_k = 12,
    f8_k = 13,
    // Common Integral:
    u64_k = 14,
    u32_k = 15,
    u16_k = 16,
    u8_k = 17,
    i64_k = 20,
    i32_k = 21,
    i16_k = 22,
    i8_k = 23,
};

enum class prefetching_kind_t {
    none_k,
    cpu_k,
    io_uring_k,
};

template <typename scalar_at> scalar_kind_t scalar_kind() noexcept {
    if (std::is_same<scalar_at, b1x8_t>())
        return scalar_kind_t::b1x8_k;
    if (std::is_same<scalar_at, uint40_t>())
        return scalar_kind_t::u40_k;
    if (std::is_same<scalar_at, uuid_t>())
        return scalar_kind_t::uuid_k;
    if (std::is_same<scalar_at, f64_t>())
        return scalar_kind_t::f64_k;
    if (std::is_same<scalar_at, f32_t>())
        return scalar_kind_t::f32_k;
    if (std::is_same<scalar_at, f16_t>())
        return scalar_kind_t::f16_k;
    if (std::is_same<scalar_at, i8_t>())
        return scalar_kind_t::i8_k;
    if (std::is_same<scalar_at, u64_t>())
        return scalar_kind_t::u64_k;
    if (std::is_same<scalar_at, u32_t>())
        return scalar_kind_t::u32_k;
    if (std::is_same<scalar_at, u16_t>())
        return scalar_kind_t::u16_k;
    if (std::is_same<scalar_at, u8_t>())
        return scalar_kind_t::u8_k;
    if (std::is_same<scalar_at, i64_t>())
        return scalar_kind_t::i64_k;
    if (std::is_same<scalar_at, i32_t>())
        return scalar_kind_t::i32_k;
    if (std::is_same<scalar_at, i16_t>())
        return scalar_kind_t::i16_k;
    if (std::is_same<scalar_at, i8_t>())
        return scalar_kind_t::i8_k;
    return scalar_kind_t::unknown_k;
}

template <typename at> at angle_to_radians(at angle) noexcept { return angle * at(3.14159265358979323846) / at(180); }

template <typename at> at square(at value) noexcept { return value * value; }

template <typename at, typename compare_at> inline at clamp(at v, at lo, at hi, compare_at comp) noexcept {
    return comp(v, lo) ? lo : comp(hi, v) ? hi : v;
}
template <typename at> inline at clamp(at v, at lo, at hi) noexcept {
    return usearch::clamp(v, lo, hi, std::less<at>{});
}

inline bool str_equals(char const* begin, std::size_t len, char const* other_begin) noexcept {
    std::size_t other_len = std::strlen(other_begin);
    return len == other_len && std::strncmp(begin, other_begin, len) == 0;
}

inline std::size_t bits_per_scalar(scalar_kind_t scalar_kind) noexcept {
    switch (scalar_kind) {
    case scalar_kind_t::f64_k: return 64;
    case scalar_kind_t::f32_k: return 32;
    case scalar_kind_t::f16_k: return 16;
    case scalar_kind_t::i8_k: return 8;
    case scalar_kind_t::b1x8_k: return 1;
    default: return 0;
    }
}

inline std::size_t bits_per_scalar_word(scalar_kind_t scalar_kind) noexcept {
    switch (scalar_kind) {
    case scalar_kind_t::f64_k: return 64;
    case scalar_kind_t::f32_k: return 32;
    case scalar_kind_t::f16_k: return 16;
    case scalar_kind_t::i8_k: return 8;
    case scalar_kind_t::b1x8_k: return 8;
    default: return 0;
    }
}

inline char const* scalar_kind_name(scalar_kind_t scalar_kind) noexcept {
    switch (scalar_kind) {
    case scalar_kind_t::f32_k: return "f32";
    case scalar_kind_t::f16_k: return "f16";
    case scalar_kind_t::f64_k: return "f64";
    case scalar_kind_t::i8_k: return "i8";
    case scalar_kind_t::b1x8_k: return "b1x8";
    default: return "";
    }
}

inline char const* metric_kind_name(metric_kind_t metric) noexcept {
    switch (metric) {
    case metric_kind_t::unknown_k: return "unknown";
    case metric_kind_t::ip_k: return "ip";
    case metric_kind_t::cos_k: return "cos";
    case metric_kind_t::l2sq_k: return "l2sq";
    case metric_kind_t::pearson_k: return "pearson";
    case metric_kind_t::haversine_k: return "haversine";
    case metric_kind_t::divergence_k: return "divergence";
    case metric_kind_t::jaccard_k: return "jaccard";
    case metric_kind_t::hamming_k: return "hamming";
    case metric_kind_t::tanimoto_k: return "tanimoto";
    case metric_kind_t::sorensen_k: return "sorensen";
    }
    return "";
}
inline expected_gt<scalar_kind_t> scalar_kind_from_name(char const* name, std::size_t len) {
    expected_gt<scalar_kind_t> parsed;
    if (str_equals(name, len, "f32"))
        parsed.result = scalar_kind_t::f32_k;
    else if (str_equals(name, len, "f64"))
        parsed.result = scalar_kind_t::f64_k;
    else if (str_equals(name, len, "f16"))
        parsed.result = scalar_kind_t::f16_k;
    else if (str_equals(name, len, "i8"))
        parsed.result = scalar_kind_t::i8_k;
    else
        parsed.failed("Unknown type, choose: f32, f16, f64, i8");
    return parsed;
}

inline expected_gt<scalar_kind_t> scalar_kind_from_name(char const* name) {
    return scalar_kind_from_name(name, std::strlen(name));
}

inline expected_gt<metric_kind_t> metric_from_name(char const* name, std::size_t len) {
    expected_gt<metric_kind_t> parsed;
    if (str_equals(name, len, "l2sq") || str_equals(name, len, "euclidean_sq")) {
        parsed.result = metric_kind_t::l2sq_k;
    } else if (str_equals(name, len, "ip") || str_equals(name, len, "inner") || str_equals(name, len, "dot")) {
        parsed.result = metric_kind_t::ip_k;
    } else if (str_equals(name, len, "cos") || str_equals(name, len, "angular")) {
        parsed.result = metric_kind_t::cos_k;
    } else if (str_equals(name, len, "haversine")) {
        parsed.result = metric_kind_t::haversine_k;
    } else if (str_equals(name, len, "divergence")) {
        parsed.result = metric_kind_t::divergence_k;
    } else if (str_equals(name, len, "pearson")) {
        parsed.result = metric_kind_t::pearson_k;
    } else if (str_equals(name, len, "hamming")) {
        parsed.result = metric_kind_t::hamming_k;
    } else if (str_equals(name, len, "tanimoto")) {
        parsed.result = metric_kind_t::tanimoto_k;
    } else if (str_equals(name, len, "sorensen")) {
        parsed.result = metric_kind_t::sorensen_k;
    } else
        parsed.failed("Unknown distance, choose: l2sq, ip, cos, haversine, divergence, jaccard, pearson, hamming, "
                      "tanimoto, sorensen");
    return parsed;
}

inline expected_gt<metric_kind_t> metric_from_name(char const* name) {
    return metric_from_name(name, std::strlen(name));
}

inline float f16_to_f32(std::uint16_t u16) noexcept {
#if !USEARCH_USE_FP16LIB
    f16_native_t f16;
    std::memcpy(&f16, &u16, sizeof(std::uint16_t));
    return float(f16);
#else
    return fp16_ieee_to_fp32_value(u16);
#endif
}

inline std::uint16_t f32_to_f16(float f32) noexcept {
#if !USEARCH_USE_FP16LIB
    f16_native_t f16 = f16_native_t(f32);
    std::uint16_t u16;
    std::memcpy(&u16, &f16, sizeof(std::uint16_t));
    return u16;
#else
    return fp16_ieee_from_fp32_value(f32);
#endif
}

/**
 *  @brief  Numeric type for the IEEE 754 half-precision floating point.
 *          If hardware support isn't available, falls back to a hardware
 *          agnostic in-software implementation.
 */
class f16_bits_t {
    std::uint16_t uint16_{};

  public:
    inline f16_bits_t() noexcept : uint16_(0) {}
    inline f16_bits_t(f16_bits_t&&) = default;
    inline f16_bits_t& operator=(f16_bits_t&&) = default;
    inline f16_bits_t(f16_bits_t const&) = default;
    inline f16_bits_t& operator=(f16_bits_t const&) = default;

    inline operator float() const noexcept { return f16_to_f32(uint16_); }
    inline explicit operator bool() const noexcept { return f16_to_f32(uint16_) > 0.5f; }

    inline f16_bits_t(i8_converted_t) noexcept;
    inline f16_bits_t(bool v) noexcept : uint16_(f32_to_f16(v)) {}
    inline f16_bits_t(float v) noexcept : uint16_(f32_to_f16(v)) {}
    inline f16_bits_t(double v) noexcept : uint16_(f32_to_f16(static_cast<float>(v))) {}

    inline f16_bits_t operator+(f16_bits_t other) const noexcept { return {float(*this) + float(other)}; }
    inline f16_bits_t operator-(f16_bits_t other) const noexcept { return {float(*this) - float(other)}; }
    inline f16_bits_t operator*(f16_bits_t other) const noexcept { return {float(*this) * float(other)}; }
    inline f16_bits_t operator/(f16_bits_t other) const noexcept { return {float(*this) / float(other)}; }
    inline f16_bits_t operator+(float other) const noexcept { return {float(*this) + other}; }
    inline f16_bits_t operator-(float other) const noexcept { return {float(*this) - other}; }
    inline f16_bits_t operator*(float other) const noexcept { return {float(*this) * other}; }
    inline f16_bits_t operator/(float other) const noexcept { return {float(*this) / other}; }
    inline f16_bits_t operator+(double other) const noexcept { return {float(*this) + other}; }
    inline f16_bits_t operator-(double other) const noexcept { return {float(*this) - other}; }
    inline f16_bits_t operator*(double other) const noexcept { return {float(*this) * other}; }
    inline f16_bits_t operator/(double other) const noexcept { return {float(*this) / other}; }

    inline f16_bits_t& operator+=(float v) noexcept {
        uint16_ = f32_to_f16(v + f16_to_f32(uint16_));
        return *this;
    }

    inline f16_bits_t& operator-=(float v) noexcept {
        uint16_ = f32_to_f16(v - f16_to_f32(uint16_));
        return *this;
    }

    inline f16_bits_t& operator*=(float v) noexcept {
        uint16_ = f32_to_f16(v * f16_to_f32(uint16_));
        return *this;
    }

    inline f16_bits_t& operator/=(float v) noexcept {
        uint16_ = f32_to_f16(v / f16_to_f32(uint16_));
        return *this;
    }
};

/**
 *  @brief  An STL-based executor or a "thread-pool" for parallel execution.
 *          Isn't efficient for small batches, as it recreates the threads on every call.
 */
class executor_stl_t {
    std::size_t threads_count_{};

    struct jthread_t {
        std::thread native_;

        jthread_t() = default;
        jthread_t(jthread_t&&) = default;
        jthread_t(jthread_t const&) = delete;
        template <typename callable_at> jthread_t(callable_at&& func) : native_([=]() { func(); }) {}

        ~jthread_t() {
            if (native_.joinable())
                native_.join();
        }
    };

  public:
    /**
     *  @param threads_count The number of threads to be used for parallel execution.
     */
    executor_stl_t(std::size_t threads_count = 0) noexcept
        : threads_count_(threads_count ? threads_count : std::thread::hardware_concurrency()) {}

    /**
     *  @return Maximum number of threads available to the executor.
     */
    std::size_t size() const noexcept { return threads_count_; }

    /**
     *  @brief Executes a fixed number of tasks using the specified thread-aware function.
     *  @param tasks                 The total number of tasks to be executed.
     *  @param thread_aware_function The thread-aware function to be called for each thread index and task index.
     *  @throws If an exception occurs during execution of the thread-aware function.
     */
    template <typename thread_aware_function_at>
    void fixed(std::size_t tasks, thread_aware_function_at&& thread_aware_function) noexcept(false) {
        std::vector<jthread_t> threads_pool;
        std::size_t tasks_per_thread = tasks;
        std::size_t threads_count = (std::min)(threads_count_, tasks);
        if (threads_count > 1) {
            tasks_per_thread = (tasks / threads_count) + ((tasks % threads_count) != 0);
            for (std::size_t thread_idx = 1; thread_idx < threads_count; ++thread_idx) {
                threads_pool.emplace_back([=]() {
                    for (std::size_t task_idx = thread_idx * tasks_per_thread;
                         task_idx < (std::min)(tasks, thread_idx * tasks_per_thread + tasks_per_thread); ++task_idx)
                        thread_aware_function(thread_idx, task_idx);
                });
            }
        }
        for (std::size_t task_idx = 0; task_idx < (std::min)(tasks, tasks_per_thread); ++task_idx)
            thread_aware_function(0, task_idx);
    }

    /**
     *  @brief Executes limited number of tasks using the specified thread-aware function.
     *  @param tasks                 The upper bound on the number of tasks.
     *  @param thread_aware_function The thread-aware function to be called for each thread index and task index.
     *  @throws If an exception occurs during execution of the thread-aware function.
     */
    template <typename thread_aware_function_at>
    void dynamic(std::size_t tasks, thread_aware_function_at&& thread_aware_function) noexcept(false) {
        std::vector<jthread_t> threads_pool;
        std::size_t tasks_per_thread = tasks;
        std::size_t threads_count = (std::min)(threads_count_, tasks);
        std::atomic_bool stop{false};
        if (threads_count > 1) {
            tasks_per_thread = (tasks / threads_count) + ((tasks % threads_count) != 0);
            for (std::size_t thread_idx = 1; thread_idx < threads_count; ++thread_idx) {
                threads_pool.emplace_back([=, &stop]() {
                    for (std::size_t task_idx = thread_idx * tasks_per_thread;
                         task_idx < (std::min)(tasks, thread_idx * tasks_per_thread + tasks_per_thread) &&
                         !stop.load(std::memory_order_relaxed);
                         ++task_idx)
                        if (!thread_aware_function(thread_idx, task_idx))
                            stop.store(true, std::memory_order_relaxed);
                });
            }
        }
        for (std::size_t task_idx = 0;
             task_idx < (std::min)(tasks, tasks_per_thread) && !stop.load(std::memory_order_relaxed); ++task_idx)
            if (!thread_aware_function(0, task_idx))
                stop.store(true, std::memory_order_relaxed);
    }

    /**
     *  @brief Saturates every available thread with the given workload, until they finish.
     *  @param thread_aware_function The thread-aware function to be called for each thread index.
     *  @throws If an exception occurs during execution of the thread-aware function.
     */
    template <typename thread_aware_function_at>
    void parallel(thread_aware_function_at&& thread_aware_function) noexcept(false) {
        if (threads_count_ == 1)
            return thread_aware_function(0);
        std::vector<jthread_t> threads_pool;
        for (std::size_t thread_idx = 1; thread_idx < threads_count_; ++thread_idx)
            threads_pool.emplace_back([=]() { thread_aware_function(thread_idx); });
        thread_aware_function(0);
    }
};

#if USEARCH_USE_OPENMP

/**
 *  @brief  An OpenMP-based executor or a "thread-pool" for parallel execution.
 *          Is the preferred implementation, when available, and maximum performance is needed.
 */
class executor_openmp_t {
  public:
    /**
     *  @param threads_count The number of threads to be used for parallel execution.
     */
    executor_openmp_t(std::size_t threads_count = 0) noexcept {
        omp_set_num_threads(static_cast<int>(threads_count ? threads_count : std::thread::hardware_concurrency()));
    }

    /**
     *  @return Maximum number of threads available to the executor.
     */
    std::size_t size() const noexcept { return omp_get_max_threads(); }

    /**
     *  @brief Executes tasks in bulk using the specified thread-aware function.
     *  @param tasks                 The total number of tasks to be executed.
     *  @param thread_aware_function The thread-aware function to be called for each thread index and task index.
     *  @throws If an exception occurs during execution of the thread-aware function.
     */
    template <typename thread_aware_function_at>
    void fixed(std::size_t tasks, thread_aware_function_at&& thread_aware_function) noexcept(false) {
#pragma omp parallel for schedule(dynamic, 1)
        for (std::size_t i = 0; i != tasks; ++i) {
            thread_aware_function(omp_get_thread_num(), i);
        }
    }

    /**
     *  @brief Executes tasks in bulk using the specified thread-aware function.
     *  @param tasks                 The total number of tasks to be executed.
     *  @param thread_aware_function The thread-aware function to be called for each thread index and task index.
     *  @throws If an exception occurs during execution of the thread-aware function.
     */
    template <typename thread_aware_function_at>
    void dynamic(std::size_t tasks, thread_aware_function_at&& thread_aware_function) noexcept(false) {
        // OpenMP cancellation points are not yet available on most platforms, and require
        // the `OMP_CANCELLATION` environment variable to be set.
        // http://jakascorner.com/blog/2016/08/omp-cancel.html
        // if (omp_get_cancellation()) {
        // #pragma omp parallel for schedule(dynamic, 1)
        //     for (std::size_t i = 0; i != tasks; ++i) {
        // #pragma omp cancellation point for
        //         if (!thread_aware_function(omp_get_thread_num(), i)) {
        // #pragma omp cancel for
        //         }
        //     }
        // }
        std::atomic_bool stop{false};
#pragma omp parallel for schedule(dynamic, 1) shared(stop)
        for (std::size_t i = 0; i != tasks; ++i) {
            if (!stop.load(std::memory_order_relaxed) && !thread_aware_function(omp_get_thread_num(), i))
                stop.store(true, std::memory_order_relaxed);
        }
    }

    /**
     *  @brief Saturates every available thread with the given workload, until they finish.
     *  @param thread_aware_function The thread-aware function to be called for each thread index.
     *  @throws If an exception occurs during execution of the thread-aware function.
     */
    template <typename thread_aware_function_at>
    void parallel(thread_aware_function_at&& thread_aware_function) noexcept(false) {
#pragma omp parallel
        { thread_aware_function(omp_get_thread_num()); }
    }
};

using executor_default_t = executor_openmp_t;

#else

using executor_default_t = executor_stl_t;

#endif

/**
 *  @brief Uses OS-specific APIs for aligned memory allocations.
 */
template <typename element_at = char, std::size_t alignment_ak = 64> //
class aligned_allocator_gt {
  public:
    using value_type = element_at;
    using size_type = std::size_t;
    using pointer = element_at*;
    using const_pointer = element_at const*;
    template <typename other_element_at> struct rebind {
        using other = aligned_allocator_gt<other_element_at>;
    };

    constexpr std::size_t alignment() const { return alignment_ak; }

    pointer allocate(size_type length) const {
        std::size_t length_bytes = alignment_ak * divide_round_up<alignment_ak>(length * sizeof(value_type));
        std::size_t alignment = alignment_ak;
        // void* result = nullptr;
        // int status = posix_memalign(&result, alignment, length_bytes);
        // return status == 0 ? (pointer)result : nullptr;
#if defined(USEARCH_DEFINED_WINDOWS)
        return (pointer)_aligned_malloc(length_bytes, alignment);
#else
        return (pointer)aligned_alloc(alignment, length_bytes);
#endif
    }

    void deallocate(pointer begin, size_type) const {
#if defined(USEARCH_DEFINED_WINDOWS)
        _aligned_free(begin);
#else
        free(begin);
#endif
    }
};

using aligned_allocator_t = aligned_allocator_gt<>;

class page_allocator_t {
  public:
    static constexpr std::size_t page_size() { return 4096; }

    /**
     *  @brief Allocates an @b uninitialized block of memory of the specified size.
     *  @param count_bytes The number of bytes to allocate.
     *  @return A pointer to the allocated memory block, or `nullptr` if allocation fails.
     */
    byte_t* allocate(std::size_t count_bytes) const noexcept {
        count_bytes = divide_round_up(count_bytes, page_size()) * page_size();
#if defined(USEARCH_DEFINED_WINDOWS)
        return (byte_t*)(::VirtualAlloc(NULL, count_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#else
        return (byte_t*)mmap(NULL, count_bytes, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
#endif
    }

    void deallocate(byte_t* page_pointer, std::size_t count_bytes) const noexcept {
#if defined(USEARCH_DEFINED_WINDOWS)
        ::VirtualFree(page_pointer, 0, MEM_RELEASE);
#else
        count_bytes = divide_round_up(count_bytes, page_size()) * page_size();
        munmap(page_pointer, count_bytes);
#endif
    }
};

/**
 *  @brief  Memory-mapping allocator designed for "alloc many, free at once" usage patterns.
 *          @b Thread-safe, @b except constructors and destructors.
 *
 *  Using this memory allocator won't affect your overall speed much, as that is not the bottleneck.
 *  However, it can drastically improve memory usage especially for huge indexes of small vectors.
 */
template <std::size_t alignment_ak = 1> class memory_mapping_allocator_gt {

    static constexpr std::size_t min_capacity() { return 1024 * 1024 * 4; }
    static constexpr std::size_t capacity_multiplier() { return 2; }
    static constexpr std::size_t head_size() {
        /// Pointer to the the previous arena and the size of the current one.
        return divide_round_up<alignment_ak>(sizeof(byte_t*) + sizeof(std::size_t)) * alignment_ak;
    }

    std::mutex mutex_;
    byte_t* last_arena_ = nullptr;
    std::size_t last_usage_ = head_size();
    std::size_t last_capacity_ = min_capacity();
    std::size_t wasted_space_ = 0;

  public:
    using value_type = byte_t;
    using size_type = std::size_t;
    using pointer = byte_t*;
    using const_pointer = byte_t const*;

    memory_mapping_allocator_gt() = default;
    memory_mapping_allocator_gt(memory_mapping_allocator_gt&& other) noexcept
        : last_arena_(exchange(other.last_arena_, nullptr)), last_usage_(exchange(other.last_usage_, 0)),
          last_capacity_(exchange(other.last_capacity_, 0)), wasted_space_(exchange(other.wasted_space_, 0)) {}

    memory_mapping_allocator_gt& operator=(memory_mapping_allocator_gt&& other) noexcept {
        std::swap(last_arena_, other.last_arena_);
        std::swap(last_usage_, other.last_usage_);
        std::swap(last_capacity_, other.last_capacity_);
        std::swap(wasted_space_, other.wasted_space_);
        return *this;
    }

    ~memory_mapping_allocator_gt() noexcept { reset(); }

    /**
     *  @brief Discards all previously allocated memory buffers.
     */
    void reset() noexcept {
        byte_t* last_arena = last_arena_;
        while (last_arena) {
            byte_t* previous_arena = nullptr;
            std::memcpy(&previous_arena, last_arena, sizeof(byte_t*));
            std::size_t last_cap = 0;
            std::memcpy(&last_cap, last_arena + sizeof(byte_t*), sizeof(std::size_t));
            page_allocator_t{}.deallocate(last_arena, last_cap);
            last_arena = previous_arena;
        }

        // Clear the references:
        last_arena_ = nullptr;
        last_usage_ = head_size();
        last_capacity_ = min_capacity();
        wasted_space_ = 0;
    }

    /**
     *  @brief Copy constructor.
     *  @note This is a no-op copy constructor since the allocator is not copyable.
     */
    memory_mapping_allocator_gt(memory_mapping_allocator_gt const&) noexcept {}

    /**
     *  @brief Copy assignment operator.
     *  @note This is a no-op copy assignment operator since the allocator is not copyable.
     *  @return Reference to the allocator after the assignment.
     */
    memory_mapping_allocator_gt& operator=(memory_mapping_allocator_gt const&) noexcept {
        reset();
        return *this;
    }

    /**
     *  @brief Allocates an @b uninitialized block of memory of the specified size.
     *  @param count_bytes The number of bytes to allocate.
     *  @return A pointer to the allocated memory block, or `nullptr` if allocation fails.
     */
    inline byte_t* allocate(std::size_t count_bytes) noexcept {
        std::size_t extended_bytes = divide_round_up<alignment_ak>(count_bytes) * alignment_ak;
        std::unique_lock<std::mutex> lock(mutex_);
        if (!last_arena_ || (last_usage_ + extended_bytes >= last_capacity_)) {
            std::size_t new_cap = (std::max)(last_capacity_, ceil2(extended_bytes)) * capacity_multiplier();
            byte_t* new_arena = page_allocator_t{}.allocate(new_cap);
            if (!new_arena)
                return nullptr;
            std::memcpy(new_arena, &last_arena_, sizeof(byte_t*));
            std::memcpy(new_arena + sizeof(byte_t*), &new_cap, sizeof(std::size_t));

            wasted_space_ += total_reserved();
            last_arena_ = new_arena;
            last_capacity_ = new_cap;
            last_usage_ = head_size();
        }

        wasted_space_ += extended_bytes - count_bytes;
        return last_arena_ + exchange(last_usage_, last_usage_ + extended_bytes);
    }

    /**
     *  @brief Returns the amount of memory used by the allocator across all arenas.
     *  @return The amount of space in bytes.
     */
    std::size_t total_allocated() const noexcept {
        if (!last_arena_)
            return 0;
        std::size_t total_used = 0;
        std::size_t last_capacity = last_capacity_;
        do {
            total_used += last_capacity;
            last_capacity /= capacity_multiplier();
        } while (last_capacity >= min_capacity());
        return total_used;
    }

    /**
     *  @brief Returns the amount of wasted space due to alignment.
     *  @return The amount of wasted space in bytes.
     */
    std::size_t total_wasted() const noexcept { return wasted_space_; }

    /**
     *  @brief Returns the amount of remaining memory already reserved but not yet used.
     *  @return The amount of reserved memory in bytes.
     */
    std::size_t total_reserved() const noexcept { return last_arena_ ? last_capacity_ - last_usage_ : 0; }

    /**
     *  @warning The very first memory de-allocation discards all the arenas!
     */
    void deallocate(byte_t* = nullptr, std::size_t = 0) noexcept { reset(); }
};

using memory_mapping_allocator_t = memory_mapping_allocator_gt<>;

/**
 *  @brief  C++11 userspace implementation of an oversimplified `std::shared_mutex`,
 *          that assumes rare interleaving of shared and unique locks. It's not fair,
 *          but requires only a single 32-bit atomic integer to work.
 */
class unfair_shared_mutex_t {
    /** Any positive integer describes the number of concurrent readers */
    enum state_t : std::int32_t {
        idle_k = 0,
        writing_k = -1,
    };
    std::atomic<std::int32_t> state_{idle_k};

  public:
    inline void lock() noexcept {
        std::int32_t raw;
    relock:
        raw = idle_k;
        if (!state_.compare_exchange_weak(raw, writing_k, std::memory_order_acquire, std::memory_order_relaxed)) {
            std::this_thread::yield();
            goto relock;
        }
    }

    inline void unlock() noexcept { state_.store(idle_k, std::memory_order_release); }

    inline void lock_shared() noexcept {
        std::int32_t raw;
    relock_shared:
        raw = state_.load(std::memory_order_acquire);
        // Spin while it's uniquely locked
        if (raw == writing_k) {
            std::this_thread::yield();
            goto relock_shared;
        }
        // Try incrementing the counter
        if (!state_.compare_exchange_weak(raw, raw + 1, std::memory_order_acquire, std::memory_order_relaxed)) {
            std::this_thread::yield();
            goto relock_shared;
        }
    }

    inline void unlock_shared() noexcept { state_.fetch_sub(1, std::memory_order_release); }

    /**
     *  @brief Try upgrades the current `lock_shared()` to a unique `lock()` state.
     */
    inline bool try_escalate() noexcept {
        std::int32_t one_read = 1;
        return state_.compare_exchange_weak(one_read, writing_k, std::memory_order_acquire, std::memory_order_relaxed);
    }

    /**
     *  @brief Escalates current lock potentially loosing control in the middle.
     *  It's a shortcut for `try_escalate`-`unlock_shared`-`lock` trio.
     */
    inline void unsafe_escalate() noexcept {
        if (!try_escalate()) {
            unlock_shared();
            lock();
        }
    }

    /**
     *  @brief Upgrades the current `lock_shared()` to a unique `lock()` state.
     */
    inline void escalate() noexcept {
        while (!try_escalate())
            std::this_thread::yield();
    }

    /**
     *  @brief De-escalation of a previously escalated state.
     */
    inline void de_escalate() noexcept {
        std::int32_t one_read = 1;
        state_.store(one_read, std::memory_order_release);
    }
};

template <typename mutex_at = unfair_shared_mutex_t> class shared_lock_gt {
    mutex_at& mutex_;

  public:
    inline explicit shared_lock_gt(mutex_at& m) noexcept : mutex_(m) { mutex_.lock_shared(); }
    inline ~shared_lock_gt() noexcept { mutex_.unlock_shared(); }
};

/**
 *  @brief  Utility class used to cast arrays of one scalar type to another,
 *          avoiding unnecessary conversions.
 */
template <typename from_scalar_at, typename to_scalar_at> struct cast_gt {
    inline bool operator()(byte_t const* input, std::size_t dim, byte_t* output) const {
        from_scalar_at const* typed_input = reinterpret_cast<from_scalar_at const*>(input);
        to_scalar_at* typed_output = reinterpret_cast<to_scalar_at*>(output);
        auto converter = [](from_scalar_at from) { return to_scalar_at(from); };
        std::transform(typed_input, typed_input + dim, typed_output, converter);
        return true;
    }
};

template <> struct cast_gt<f32_t, f32_t> {
    bool operator()(byte_t const*, std::size_t, byte_t*) const { return false; }
};

template <> struct cast_gt<f64_t, f64_t> {
    bool operator()(byte_t const*, std::size_t, byte_t*) const { return false; }
};

template <> struct cast_gt<f16_bits_t, f16_bits_t> {
    bool operator()(byte_t const*, std::size_t, byte_t*) const { return false; }
};

template <> struct cast_gt<i8_t, i8_t> {
    bool operator()(byte_t const*, std::size_t, byte_t*) const { return false; }
};

template <> struct cast_gt<b1x8_t, b1x8_t> {
    bool operator()(byte_t const*, std::size_t, byte_t*) const { return false; }
};

template <typename from_scalar_at> struct cast_gt<from_scalar_at, b1x8_t> {
    inline bool operator()(byte_t const* input, std::size_t dim, byte_t* output) const {
        from_scalar_at const* typed_input = reinterpret_cast<from_scalar_at const*>(input);
        unsigned char* typed_output = reinterpret_cast<unsigned char*>(output);
        for (std::size_t i = 0; i != dim; ++i)
            // Converting from scalar types to boolean isn't trivial and depends on the type.
            // The most common case is to consider all positive values as `true` and all others as `false`.
            //  - `bool(0.00001f)` converts to 1
            //  - `bool(-0.00001f)` converts to 1
            //  - `bool(0)` converts to 0
            //  - `bool(-0)` converts to 0
            //  - `bool(std::numeric_limits<float>::infinity())` converts to 1
            //  - `bool(std::numeric_limits<float>::epsilon())` converts to 1
            //  - `bool(std::numeric_limits<float>::signaling_NaN())` converts to 1
            //  - `bool(std::numeric_limits<float>::denorm_min())` converts to 1
            typed_output[i / CHAR_BIT] |= bool(typed_input[i] > 0) ? (128 >> (i & (CHAR_BIT - 1))) : 0;
        return true;
    }
};

template <typename to_scalar_at> struct cast_gt<b1x8_t, to_scalar_at> {
    inline bool operator()(byte_t const* input, std::size_t dim, byte_t* output) const {
        unsigned char const* typed_input = reinterpret_cast<unsigned char const*>(input);
        to_scalar_at* typed_output = reinterpret_cast<to_scalar_at*>(output);
        for (std::size_t i = 0; i != dim; ++i)
            // We can't entirely reconstruct the original scalar type from a boolean.
            // The simplest variant would be to map set bits to ones, and unset bits to zeros.
            typed_output[i] = bool(typed_input[i / CHAR_BIT] & (128 >> (i & (CHAR_BIT - 1))));
        return true;
    }
};

/**
 *  @brief  Numeric type for uniformly-distributed floating point
 *          values within [-1,1] range, quantized to integers [-100,100].
 */
class i8_converted_t {
    std::int8_t int8_{};

  public:
    constexpr static f32_t divisor_k = 100.f;
    constexpr static std::int8_t min_k = -100;
    constexpr static std::int8_t max_k = 100;

    inline i8_converted_t() noexcept : int8_(0) {}
    inline i8_converted_t(bool v) noexcept : int8_(v ? max_k : 0) {}

    inline i8_converted_t(i8_converted_t&&) = default;
    inline i8_converted_t& operator=(i8_converted_t&&) = default;
    inline i8_converted_t(i8_converted_t const&) = default;
    inline i8_converted_t& operator=(i8_converted_t const&) = default;

    inline operator f16_t() const noexcept { return static_cast<f16_t>(f32_t(int8_) / divisor_k); }
    inline operator f32_t() const noexcept { return f32_t(int8_) / divisor_k; }
    inline operator f64_t() const noexcept { return f64_t(int8_) / divisor_k; }
    inline explicit operator bool() const noexcept { return int8_ > (max_k / 2); }
    inline explicit operator std::int8_t() const noexcept { return int8_; }
    inline explicit operator std::int16_t() const noexcept { return int8_; }
    inline explicit operator std::int32_t() const noexcept { return int8_; }
    inline explicit operator std::int64_t() const noexcept { return int8_; }

    inline i8_converted_t(f16_t v)
        : int8_(usearch::clamp<std::int8_t>(static_cast<std::int8_t>(v * divisor_k), min_k, max_k)) {}
    inline i8_converted_t(f32_t v)
        : int8_(usearch::clamp<std::int8_t>(static_cast<std::int8_t>(v * divisor_k), min_k, max_k)) {}
    inline i8_converted_t(f64_t v)
        : int8_(usearch::clamp<std::int8_t>(static_cast<std::int8_t>(v * divisor_k), min_k, max_k)) {}
};

f16_bits_t::f16_bits_t(i8_converted_t v) noexcept : uint16_(f32_to_f16(v)) {}

template <> struct cast_gt<i8_t, f16_t> : public cast_gt<i8_converted_t, f16_t> {};
template <> struct cast_gt<i8_t, f32_t> : public cast_gt<i8_converted_t, f32_t> {};
template <> struct cast_gt<i8_t, f64_t> : public cast_gt<i8_converted_t, f64_t> {};

template <> struct cast_gt<f16_t, i8_t> : public cast_gt<f16_t, i8_converted_t> {};
template <> struct cast_gt<f32_t, i8_t> : public cast_gt<f32_t, i8_converted_t> {};
template <> struct cast_gt<f64_t, i8_t> : public cast_gt<f64_t, i8_converted_t> {};

/**
 *  @brief  Inner (Dot) Product distance.
 */
template <typename scalar_at = float, typename result_at = scalar_at> struct metric_ip_gt {
    using scalar_t = scalar_at;
    using result_t = result_at;

    inline result_t operator()(scalar_t const* a, scalar_t const* b, std::size_t dim) const noexcept {
        result_t ab{};
#if USEARCH_USE_OPENMP
#pragma omp simd reduction(+ : ab)
#elif defined(USEARCH_DEFINED_CLANG)
#pragma clang loop vectorize(enable)
#elif defined(USEARCH_DEFINED_GCC)
#pragma GCC ivdep
#endif
        for (std::size_t i = 0; i != dim; ++i)
            ab += result_t(a[i]) * result_t(b[i]);
        return 1 - ab;
    }
};

/**
 *  @brief  Cosine (Angular) distance.
 *          Identical to the Inner Product of normalized vectors.
 *          Unless you are running on an tiny embedded platform, this metric
 *          is recommended over `::metric_ip_gt` for low-precision scalars.
 */
template <typename scalar_at = float, typename result_at = scalar_at> struct metric_cos_gt {
    using scalar_t = scalar_at;
    using result_t = result_at;

    inline result_t operator()(scalar_t const* a, scalar_t const* b, std::size_t dim) const noexcept {
        result_t ab{}, a2{}, b2{};
#if USEARCH_USE_OPENMP
#pragma omp simd reduction(+ : ab, a2, b2)
#elif defined(USEARCH_DEFINED_CLANG)
#pragma clang loop vectorize(enable)
#elif defined(USEARCH_DEFINED_GCC)
#pragma GCC ivdep
#endif
        for (std::size_t i = 0; i != dim; ++i) {
            result_t ai = static_cast<result_t>(a[i]);
            result_t bi = static_cast<result_t>(b[i]);
            ab += ai * bi, a2 += square(ai), b2 += square(bi);
        }

        result_t result_if_zero[2][2];
        result_if_zero[0][0] = 1 - ab / (std::sqrt(a2) * std::sqrt(b2));
        result_if_zero[0][1] = result_if_zero[1][0] = 1;
        result_if_zero[1][1] = 0;
        return result_if_zero[a2 == 0][b2 == 0];
    }
};

/**
 *  @brief  Squared Euclidean (L2) distance.
 *          Square root is avoided at the end, as it won't affect the ordering.
 */
template <typename scalar_at = float, typename result_at = scalar_at> struct metric_l2sq_gt {
    using scalar_t = scalar_at;
    using result_t = result_at;

    inline result_t operator()(scalar_t const* a, scalar_t const* b, std::size_t dim) const noexcept {
        result_t ab_deltas_sq{};
#if USEARCH_USE_OPENMP
#pragma omp simd reduction(+ : ab_deltas_sq)
#elif defined(USEARCH_DEFINED_CLANG)
#pragma clang loop vectorize(enable)
#elif defined(USEARCH_DEFINED_GCC)
#pragma GCC ivdep
#endif
        for (std::size_t i = 0; i != dim; ++i) {
            result_t ai = static_cast<result_t>(a[i]);
            result_t bi = static_cast<result_t>(b[i]);
            ab_deltas_sq += square(ai - bi);
        }
        return ab_deltas_sq;
    }
};

/**
 *  @brief  Hamming distance computes the number of differing bits in
 *          two arrays of integers. An example would be a textual document,
 *          tokenized and hashed into a fixed-capacity bitset.
 */
template <typename scalar_at = std::uint64_t, typename result_at = std::size_t> struct metric_hamming_gt {
    using scalar_t = scalar_at;
    using result_t = result_at;
    static_assert( //
        std::is_unsigned<scalar_t>::value ||
            (std::is_enum<scalar_t>::value && std::is_unsigned<typename std::underlying_type<scalar_t>::type>::value),
        "Hamming distance requires unsigned integral words");

    inline result_t operator()(scalar_t const* a, scalar_t const* b, std::size_t words) const noexcept {
        constexpr std::size_t bits_per_word_k = sizeof(scalar_t) * CHAR_BIT;
        result_t matches{};
#if USEARCH_USE_OPENMP
#pragma omp simd reduction(+ : matches)
#elif defined(USEARCH_DEFINED_CLANG)
#pragma clang loop vectorize(enable)
#elif defined(USEARCH_DEFINED_GCC)
#pragma GCC ivdep
#endif
        for (std::size_t i = 0; i != words; ++i)
            matches += std::bitset<bits_per_word_k>(a[i] ^ b[i]).count();
        return matches;
    }
};

/**
 *  @brief  Tanimoto distance is the intersection over bitwise union.
 *          Often used in chemistry and biology to compare molecular fingerprints.
 */
template <typename scalar_at = std::uint64_t, typename result_at = float> struct metric_tanimoto_gt {
    using scalar_t = scalar_at;
    using result_t = result_at;
    static_assert( //
        std::is_unsigned<scalar_t>::value ||
            (std::is_enum<scalar_t>::value && std::is_unsigned<typename std::underlying_type<scalar_t>::type>::value),
        "Tanimoto distance requires unsigned integral words");
    static_assert(std::is_floating_point<result_t>::value, "Tanimoto distance will be a fraction");

    inline result_t operator()(scalar_t const* a, scalar_t const* b, std::size_t words) const noexcept {
        constexpr std::size_t bits_per_word_k = sizeof(scalar_t) * CHAR_BIT;
        result_t and_count{};
        result_t or_count{};
#if USEARCH_USE_OPENMP
#pragma omp simd reduction(+ : and_count, or_count)
#elif defined(USEARCH_DEFINED_CLANG)
#pragma clang loop vectorize(enable)
#elif defined(USEARCH_DEFINED_GCC)
#pragma GCC ivdep
#endif
        for (std::size_t i = 0; i != words; ++i) {
            and_count += std::bitset<bits_per_word_k>(a[i] & b[i]).count();
            or_count += std::bitset<bits_per_word_k>(a[i] | b[i]).count();
        }
        return 1 - result_t(and_count) / or_count;
    }
};

/**
 *  @brief  Sorensen-Dice or F1 distance is the intersection over bitwise union.
 *          Often used in chemistry and biology to compare molecular fingerprints.
 */
template <typename scalar_at = std::uint64_t, typename result_at = float> struct metric_sorensen_gt {
    using scalar_t = scalar_at;
    using result_t = result_at;
    static_assert( //
        std::is_unsigned<scalar_t>::value ||
            (std::is_enum<scalar_t>::value && std::is_unsigned<typename std::underlying_type<scalar_t>::type>::value),
        "Sorensen-Dice distance requires unsigned integral words");
    static_assert(std::is_floating_point<result_t>::value, "Sorensen-Dice distance will be a fraction");

    inline result_t operator()(scalar_t const* a, scalar_t const* b, std::size_t words) const noexcept {
        constexpr std::size_t bits_per_word_k = sizeof(scalar_t) * CHAR_BIT;
        result_t and_count{};
        result_t any_count{};
#if USEARCH_USE_OPENMP
#pragma omp simd reduction(+ : and_count, any_count)
#elif defined(USEARCH_DEFINED_CLANG)
#pragma clang loop vectorize(enable)
#elif defined(USEARCH_DEFINED_GCC)
#pragma GCC ivdep
#endif
        for (std::size_t i = 0; i != words; ++i) {
            and_count += std::bitset<bits_per_word_k>(a[i] & b[i]).count();
            any_count += std::bitset<bits_per_word_k>(a[i]).count() + std::bitset<bits_per_word_k>(b[i]).count();
        }
        return 1 - 2 * result_t(and_count) / any_count;
    }
};

/**
 *  @brief  Counts the number of matching elements in two unique sorted sets.
 *          Can be used to compute the similarity between two textual documents
 *          using the IDs of tokens present in them.
 *          Similar to `metric_tanimoto_gt` for dense representations.
 */
template <typename scalar_at = std::int32_t, typename result_at = float> struct metric_jaccard_gt {
    using scalar_t = scalar_at;
    using result_t = result_at;
    static_assert(!std::is_floating_point<scalar_t>::value, "Jaccard distance requires integral scalars");

    inline result_t operator()( //
        scalar_t const* a, scalar_t const* b, std::size_t a_length, std::size_t b_length) const noexcept {
        result_t intersection{};
        std::size_t i{};
        std::size_t j{};
        while (i != a_length && j != b_length) {
            intersection += a[i] == b[j];
            i += a[i] < b[j];
            j += a[i] >= b[j];
        }
        return 1 - intersection / (a_length + b_length - intersection);
    }
};

/**
 *  @brief  Measures Pearson Correlation between two sequences in a single pass.
 */
template <typename scalar_at = float, typename result_at = float> struct metric_pearson_gt {
    using scalar_t = scalar_at;
    using result_t = result_at;

    inline result_t operator()(scalar_t const* a, scalar_t const* b, std::size_t dim) const noexcept {
        // The correlation coefficient can't be defined for one or zero-dimensional data.
        if (dim <= 1)
            return 0;
        // Conventional Pearson Correlation Coefficient definiton subtracts the mean value of each
        // sequence from each element, before dividing them. WikiPedia article suggests a convenient
        // single-pass algorithm for calculating sample correlations, though depending on the numbers
        // involved, it can sometimes be numerically unstable.
        result_t a_sum{}, b_sum{}, ab_sum{};
        result_t a_sq_sum{}, b_sq_sum{};
#if USEARCH_USE_OPENMP
#pragma omp simd reduction(+ : a_sum, b_sum, ab_sum, a_sq_sum, b_sq_sum)
#elif defined(USEARCH_DEFINED_CLANG)
#pragma clang loop vectorize(enable)
#elif defined(USEARCH_DEFINED_GCC)
#pragma GCC ivdep
#endif
        for (std::size_t i = 0; i != dim; ++i) {
            result_t ai = static_cast<result_t>(a[i]);
            result_t bi = static_cast<result_t>(b[i]);
            a_sum += ai;
            b_sum += bi;
            ab_sum += ai * bi;
            a_sq_sum += ai * ai;
            b_sq_sum += bi * bi;
        }
        result_t denom = (dim * a_sq_sum - a_sum * a_sum) * (dim * b_sq_sum - b_sum * b_sum);
        if (denom == 0)
            return 0;
        result_t corr = dim * ab_sum - a_sum * b_sum;
        denom = std::sqrt(denom);
        // The normal Pearson correlation value is between -1 and 1, but we are looking for a distance.
        // So instead of returning `corr / denom`, we return `1 - corr / denom`.
        return 1 - corr / denom;
    }
};

/**
 *  @brief  Measures Jensen-Shannon Divergence between two probability distributions.
 */
template <typename scalar_at = float, typename result_at = float> struct metric_divergence_gt {
    using scalar_t = scalar_at;
    using result_t = result_at;

    inline result_t operator()(scalar_t const* p, scalar_t const* q, std::size_t dim) const noexcept {
        result_t kld_pm{}, kld_qm{};
        result_t epsilon = std::numeric_limits<result_t>::epsilon();
#if USEARCH_USE_OPENMP
#pragma omp simd reduction(+ : kld_pm, kld_qm)
#elif defined(USEARCH_DEFINED_CLANG)
#pragma clang loop vectorize(enable)
#elif defined(USEARCH_DEFINED_GCC)
#pragma GCC ivdep
#endif
        for (std::size_t i = 0; i != dim; ++i) {
            result_t pi = static_cast<result_t>(p[i]);
            result_t qi = static_cast<result_t>(q[i]);
            result_t mi = (pi + qi) / 2 + epsilon;
            kld_pm += pi * std::log((pi + epsilon) / mi);
            kld_qm += qi * std::log((qi + epsilon) / mi);
        }
        return (kld_pm + kld_qm) / 2;
    }
};

struct cos_i8_t {
    using scalar_t = i8_t;
    using result_t = f32_t;

    inline result_t operator()(i8_t const* a, i8_t const* b, std::size_t dim) const noexcept {
        std::int32_t ab{}, a2{}, b2{};
#if USEARCH_USE_OPENMP
#pragma omp simd reduction(+ : ab, a2, b2)
#elif defined(USEARCH_DEFINED_CLANG)
#pragma clang loop vectorize(enable)
#elif defined(USEARCH_DEFINED_GCC)
#pragma GCC ivdep
#endif
        for (std::size_t i = 0; i != dim; i++) {
            std::int16_t ai{a[i]};
            std::int16_t bi{b[i]};
            ab += ai * bi;
            a2 += square(ai);
            b2 += square(bi);
        }
        result_t a2f = std::sqrt(static_cast<result_t>(a2));
        result_t b2f = std::sqrt(static_cast<result_t>(b2));
        return (ab != 0) ? (1.f - ab / (a2f * b2f)) : 0;
    }
};

struct l2sq_i8_t {
    using scalar_t = i8_t;
    using result_t = f32_t;

    inline result_t operator()(i8_t const* a, i8_t const* b, std::size_t dim) const noexcept {
        std::int32_t ab_deltas_sq{};
#if USEARCH_USE_OPENMP
#pragma omp simd reduction(+ : ab_deltas_sq)
#elif defined(USEARCH_DEFINED_CLANG)
#pragma clang loop vectorize(enable)
#elif defined(USEARCH_DEFINED_GCC)
#pragma GCC ivdep
#endif
        for (std::size_t i = 0; i != dim; i++)
            ab_deltas_sq += square(std::int16_t(a[i]) - std::int16_t(b[i]));
        return static_cast<result_t>(ab_deltas_sq);
    }
};

/**
 *  @brief  Haversine distance for the shortest distance between two nodes on
 *          the surface of a 3D sphere, defined with latitude and longitude.
 */
template <typename scalar_at = float, typename result_at = scalar_at> struct metric_haversine_gt {
    using scalar_t = scalar_at;
    using result_t = result_at;
    static_assert(!std::is_integral<scalar_t>::value, "Latitude and longitude must be floating-node");

    inline result_t operator()(scalar_t const* a, scalar_t const* b, std::size_t = 2) const noexcept {
        result_t lat_a = a[0], lon_a = a[1];
        result_t lat_b = b[0], lon_b = b[1];

        result_t lat_delta = angle_to_radians<result_t>(lat_b - lat_a) / 2;
        result_t lon_delta = angle_to_radians<result_t>(lon_b - lon_a) / 2;

        result_t converted_lat_a = angle_to_radians<result_t>(lat_a);
        result_t converted_lat_b = angle_to_radians<result_t>(lat_b);

        result_t x = square(std::sin(lat_delta)) + //
                     std::cos(converted_lat_a) * std::cos(converted_lat_b) * square(std::sin(lon_delta));

        return 2 * std::asin(std::sqrt(x));
    }
};

using distance_punned_t = float;
using span_punned_t = span_gt<byte_t const>;

/**
 *  @brief  The signature of the user-defined function.
 *          Can be just two array pointers, precompiled for a specific array length,
 *          or include one or two array sizes as 64-bit unsigned integers.
 */
enum class metric_punned_signature_t {
    array_array_k = 0,
    array_array_size_k,
    array_array_state_k,
};

/**
 *  @brief  Type-punned metric class, which unlike STL's `std::function` avoids any memory allocations.
 *          It also provides additional APIs to check, if SIMD hardware-acceleration is available.
 *          Wraps the `simsimd_metric_punned_t` when available. The auto-vectorized backend otherwise.
 */
class metric_punned_t {
  public:
    using scalar_t = byte_t;
    using result_t = distance_punned_t;

  private:
    /// In the generalized function API all the are arguments are pointer-sized.
    using uptr_t = std::size_t;
    /// Distance function that takes two arrays and returns a scalar.
    using metric_array_array_t = result_t (*)(uptr_t, uptr_t);
    /// Distance function that takes two arrays and their length and returns a scalar.
    using metric_array_array_size_t = result_t (*)(uptr_t, uptr_t, uptr_t);
    /// Distance function that takes two arrays and some callback state and returns a scalar.
    using metric_array_array_state_t = result_t (*)(uptr_t, uptr_t, uptr_t);
    /// Distance function callback, like `metric_array_array_size_t`, but depends on member variables.
    using metric_rounted_t = result_t (metric_punned_t::*)(uptr_t, uptr_t) const;

    metric_rounted_t metric_routed_ = nullptr;
    uptr_t metric_ptr_ = 0;
    uptr_t metric_third_arg_ = 0;

    std::size_t dimensions_ = 0;
    metric_kind_t metric_kind_ = metric_kind_t::unknown_k;
    scalar_kind_t scalar_kind_ = scalar_kind_t::unknown_k;

#if USEARCH_USE_SIMSIMD
    simsimd_capability_t isa_kind_ = simsimd_cap_serial_k;
#endif

  public:
    /**
     *  @brief  Computes the distance between two vectors of fixed length.
     *
     *  ! This is the only relevant function in the object. Everything else is just dynamic dispatch logic.
     */
    inline result_t operator()(byte_t const* a, byte_t const* b) const noexcept {
        return (this->*metric_routed_)(reinterpret_cast<uptr_t>(a), reinterpret_cast<uptr_t>(b));
    }

    inline metric_punned_t() noexcept = default;
    inline metric_punned_t(metric_punned_t const&) noexcept = default;
    inline metric_punned_t& operator=(metric_punned_t const&) noexcept = default;

    inline metric_punned_t(std::size_t dimensions, metric_kind_t metric_kind = metric_kind_t::l2sq_k,
                           scalar_kind_t scalar_kind = scalar_kind_t::f32_k) noexcept
        : metric_punned_t(builtin(dimensions, metric_kind, scalar_kind)) {}

    inline metric_punned_t(std::size_t dimensions, std::uintptr_t metric_uintptr, metric_punned_signature_t signature,
                           metric_kind_t metric_kind, scalar_kind_t scalar_kind) noexcept
        : metric_punned_t(stateless(dimensions, metric_uintptr, signature, metric_kind, scalar_kind)) {}

    /**
     *  @brief  Creates a metric of a natively supported kind, choosing the best
     *          available backend internally or from SimSIMD.
     *
     *  @param  dimensions      The number of elements in the input arrays.
     *  @param  metric_kind     The kind of metric to use.
     *  @param  scalar_kind     The kind of scalar to use.
     *  @return                 A metric object that can be used to compute distances between vectors.
     */
    inline static metric_punned_t builtin(std::size_t dimensions, metric_kind_t metric_kind = metric_kind_t::l2sq_k,
                                          scalar_kind_t scalar_kind = scalar_kind_t::f32_k) noexcept {
        metric_punned_t metric;
        metric.metric_routed_ = &metric_punned_t::invoke_array_array_third;
        metric.metric_ptr_ = 0;
        metric.metric_third_arg_ =
            scalar_kind == scalar_kind_t::b1x8_k ? divide_round_up<CHAR_BIT>(dimensions) : dimensions;
        metric.dimensions_ = dimensions;
        metric.metric_kind_ = metric_kind;
        metric.scalar_kind_ = scalar_kind;

#if USEARCH_USE_SIMSIMD
        if (!metric.configure_with_simsimd())
            metric.configure_with_autovec();
#else
        metric.configure_with_autovec();
#endif

        return metric;
    }

    /**
     *  @brief  Creates a metric using the provided function pointer for a stateless metric.
     *          So the provided ::metric_uintptr is a pointer to a function that takes two arrays
     *          and returns a scalar. If the ::signature is metric_punned_signature_t::array_array_size_k,
     *          then the third argument is the number of scalar words in the input vectors.
     *
     *  @param  dimensions      The number of elements in the input arrays.
     *  @param  metric_uintptr  The function pointer to the metric function.
     *  @param  signature       The signature of the metric function.
     *  @param  metric_kind     The kind of metric to use.
     *  @param  scalar_kind     The kind of scalar to use.
     *  @return                 A metric object that can be used to compute distances between vectors.
     */
    inline static metric_punned_t stateless(std::size_t dimensions, std::uintptr_t metric_uintptr,
                                            metric_punned_signature_t signature, metric_kind_t metric_kind,
                                            scalar_kind_t scalar_kind) noexcept {
        metric_punned_t metric;
        metric.metric_routed_ = signature == metric_punned_signature_t::array_array_k
                                    ? &metric_punned_t::invoke_array_array
                                    : &metric_punned_t::invoke_array_array_third;
        metric.metric_ptr_ = metric_uintptr;
        metric.metric_third_arg_ =
            scalar_kind == scalar_kind_t::b1x8_k ? divide_round_up<CHAR_BIT>(dimensions) : dimensions;
        metric.dimensions_ = dimensions;
        metric.metric_kind_ = metric_kind;
        metric.scalar_kind_ = scalar_kind;
        return metric;
    }

    /**
     *  @brief  Creates a metric using the provided function pointer for a statefull metric.
     *          The third argument is the state that will be passed to the metric function.
     *
     *  @param  metric_uintptr  The function pointer to the metric function.
     *  @param  metric_state    The state to pass to the metric function.
     *  @param  metric_kind     The kind of metric to use.
     *  @param  scalar_kind     The kind of scalar to use.
     *  @return                 A metric object that can be used to compute distances between vectors.
     */
    inline static metric_punned_t statefull(std::uintptr_t metric_uintptr, std::uintptr_t metric_state,
                                            metric_kind_t metric_kind = metric_kind_t::unknown_k,
                                            scalar_kind_t scalar_kind = scalar_kind_t::unknown_k) noexcept {
        metric_punned_t metric;
        metric.metric_routed_ = &metric_punned_t::invoke_array_array_third;
        metric.metric_ptr_ = metric_uintptr;
        metric.metric_third_arg_ = metric_state;
        metric.dimensions_ = 0;
        metric.metric_kind_ = metric_kind;
        metric.scalar_kind_ = scalar_kind;
        return metric;
    }

    inline std::size_t dimensions() const noexcept { return dimensions_; }
    inline metric_kind_t metric_kind() const noexcept { return metric_kind_; }
    inline scalar_kind_t scalar_kind() const noexcept { return scalar_kind_; }
    inline explicit operator bool() const noexcept { return metric_routed_ && metric_ptr_; }

    /**
     *  @brief  Checks fi we've failed to initialized the metric with provided arguments.
     *
     *  It's different from `operator bool()` when it comes to explicitly uninitialized metrics.
     *  It's a common case, where a NULL state is created only to be overwritten later, when
     *  we recover an old index state from a file or a network.
     */
    inline bool missing() const noexcept { return !bool(*this) && metric_kind_ != metric_kind_t::unknown_k; }

    inline char const* isa_name() const noexcept {
        if (!*this)
            return "uninitialized";

#if USEARCH_USE_SIMSIMD
        switch (isa_kind_) {
        case simsimd_cap_serial_k: return "serial";
        case simsimd_cap_neon_k: return "neon";
        case simsimd_cap_sve_k: return "sve";
        case simsimd_cap_haswell_k: return "haswell";
        case simsimd_cap_skylake_k: return "skylake";
        case simsimd_cap_ice_k: return "ice";
        case simsimd_cap_sapphire_k: return "sapphire";
        default: return "unknown";
        }
#endif
        return "serial";
    }

    inline std::size_t bytes_per_vector() const noexcept {
        return divide_round_up<CHAR_BIT>(dimensions_ * bits_per_scalar(scalar_kind_));
    }

    inline std::size_t scalar_words() const noexcept {
        return divide_round_up(dimensions_ * bits_per_scalar(scalar_kind_), bits_per_scalar_word(scalar_kind_));
    }

  private:
#if USEARCH_USE_SIMSIMD
    bool configure_with_simsimd(simsimd_capability_t simd_caps) noexcept {
        simsimd_metric_kind_t kind = simsimd_metric_unknown_k;
        simsimd_datatype_t datatype = simsimd_datatype_unknown_k;
        simsimd_capability_t allowed = simsimd_cap_any_k;
        switch (metric_kind_) {
        case metric_kind_t::ip_k: kind = simsimd_metric_dot_k; break;
        case metric_kind_t::cos_k: kind = simsimd_metric_cos_k; break;
        case metric_kind_t::l2sq_k: kind = simsimd_metric_l2sq_k; break;
        case metric_kind_t::hamming_k: kind = simsimd_metric_hamming_k; break;
        case metric_kind_t::tanimoto_k: kind = simsimd_metric_jaccard_k; break;
        case metric_kind_t::jaccard_k: kind = simsimd_metric_jaccard_k; break;
        default: break;
        }
        switch (scalar_kind_) {
        case scalar_kind_t::f32_k: datatype = simsimd_datatype_f32_k; break;
        case scalar_kind_t::f64_k: datatype = simsimd_datatype_f64_k; break;
        case scalar_kind_t::f16_k: datatype = simsimd_datatype_f16_k; break;
        case scalar_kind_t::i8_k: datatype = simsimd_datatype_i8_k; break;
        case scalar_kind_t::b1x8_k: datatype = simsimd_datatype_b8_k; break;
        default: break;
        }
        simsimd_metric_punned_t simd_metric = NULL;
        simsimd_capability_t simd_kind = simsimd_cap_any_k;
        simsimd_find_metric_punned(kind, datatype, simd_caps, allowed, &simd_metric, &simd_kind);
        if (simd_metric == nullptr)
            return false;

        std::memcpy(&metric_ptr_, &simd_metric, sizeof(simd_metric));
        metric_routed_ = metric_kind_ == metric_kind_t::ip_k
                             ? reinterpret_cast<metric_rounted_t>(&metric_punned_t::invoke_simsimd_reverse)
                             : reinterpret_cast<metric_rounted_t>(&metric_punned_t::invoke_simsimd);
        isa_kind_ = simd_kind;
        return true;
    }
    bool configure_with_simsimd() noexcept {
        static simsimd_capability_t static_capabilities = simsimd_capabilities();
        return configure_with_simsimd(static_capabilities);
    }
    result_t invoke_simsimd(uptr_t a, uptr_t b) const noexcept {
        simsimd_distance_t result;
        // Here `reinterpret_cast` raises warning... we know what we are doing!
        auto function_pointer = (simsimd_metric_punned_t)(metric_ptr_);
        function_pointer(reinterpret_cast<void const*>(a), reinterpret_cast<void const*>(b), metric_third_arg_,
                         &result);
        return (result_t)result;
    }
    result_t invoke_simsimd_reverse(uptr_t a, uptr_t b) const noexcept { return 1 - invoke_simsimd(a, b); }
#else
    bool configure_with_simsimd() noexcept { return false; }
#endif
    result_t invoke_array_array_third(uptr_t a, uptr_t b) const noexcept {
        auto function_pointer = (metric_array_array_size_t)(metric_ptr_);
        result_t result = function_pointer(a, b, metric_third_arg_);
        return result;
    }
    result_t invoke_array_array(uptr_t a, uptr_t b) const noexcept {
        auto function_pointer = (metric_array_array_t)(metric_ptr_);
        result_t result = function_pointer(a, b);
        return result;
    }
    void configure_with_autovec() noexcept {
        switch (metric_kind_) {
        case metric_kind_t::ip_k: {
            switch (scalar_kind_) {
            case scalar_kind_t::f32_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_ip_gt<f32_t>>; break;
            case scalar_kind_t::f16_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_ip_gt<f16_t, f32_t>>; break;
            case scalar_kind_t::i8_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_ip_gt<i8_t, f32_t>>; break;
            case scalar_kind_t::f64_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_ip_gt<f64_t>>; break;
            default: metric_ptr_ = 0; break;
            }
            break;
        }
        case metric_kind_t::cos_k: {
            switch (scalar_kind_) {
            case scalar_kind_t::f32_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_cos_gt<f32_t>>; break;
            case scalar_kind_t::f16_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_cos_gt<f16_t, f32_t>>; break;
            case scalar_kind_t::i8_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_cos_gt<i8_t, f32_t>>; break;
            case scalar_kind_t::f64_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_cos_gt<f64_t>>; break;
            default: metric_ptr_ = 0; break;
            }
            break;
        }
        case metric_kind_t::l2sq_k: {
            switch (scalar_kind_) {
            case scalar_kind_t::f32_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_l2sq_gt<f32_t>>; break;
            case scalar_kind_t::f16_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_l2sq_gt<f16_t, f32_t>>; break;
            case scalar_kind_t::i8_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_l2sq_gt<i8_t, f32_t>>; break;
            case scalar_kind_t::f64_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_l2sq_gt<f64_t>>; break;
            default: metric_ptr_ = 0; break;
            }
            break;
        }
        case metric_kind_t::pearson_k: {
            switch (scalar_kind_) {
            case scalar_kind_t::i8_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_pearson_gt<i8_t, f32_t>>; break;
            case scalar_kind_t::f16_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_pearson_gt<f16_t, f32_t>>; break;
            case scalar_kind_t::f32_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_pearson_gt<f32_t>>; break;
            case scalar_kind_t::f64_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_pearson_gt<f64_t>>; break;
            default: metric_ptr_ = 0; break;
            }
            break;
        }
        case metric_kind_t::haversine_k: {
            switch (scalar_kind_) {
            case scalar_kind_t::f16_k:
                metric_ptr_ = (uptr_t)&equidimensional_<metric_haversine_gt<f16_t, f32_t>>;
                break;
            case scalar_kind_t::f32_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_haversine_gt<f32_t>>; break;
            case scalar_kind_t::f64_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_haversine_gt<f64_t>>; break;
            default: metric_ptr_ = 0; break;
            }
            break;
        }
        case metric_kind_t::divergence_k: {
            switch (scalar_kind_) {
            case scalar_kind_t::f16_k:
                metric_ptr_ = (uptr_t)&equidimensional_<metric_divergence_gt<f16_t, f32_t>>;
                break;
            case scalar_kind_t::f32_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_divergence_gt<f32_t>>; break;
            case scalar_kind_t::f64_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_divergence_gt<f64_t>>; break;
            default: metric_ptr_ = 0; break;
            }
            break;
        }
        case metric_kind_t::jaccard_k: // Equivalent to Tanimoto
        case metric_kind_t::tanimoto_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_tanimoto_gt<b1x8_t>>; break;
        case metric_kind_t::hamming_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_hamming_gt<b1x8_t>>; break;
        case metric_kind_t::sorensen_k: metric_ptr_ = (uptr_t)&equidimensional_<metric_sorensen_gt<b1x8_t>>; break;
        default: return;
        }
    }

    template <typename typed_at>
    inline static result_t equidimensional_(uptr_t a, uptr_t b, uptr_t a_dimensions) noexcept {
        using scalar_t = typename typed_at::scalar_t;
        return static_cast<result_t>(typed_at{}((scalar_t const*)a, (scalar_t const*)b, a_dimensions));
    }
};

/**
 *  @brief  View over a potentially-strided memory buffer, containing a row-major matrix.
 */
template <typename scalar_at> //
class vectors_view_gt {
    using scalar_t = scalar_at;

    scalar_t const* begin_{};
    std::size_t dimensions_{};
    std::size_t count_{};
    std::size_t stride_bytes_{};

  public:
    vectors_view_gt() noexcept = default;
    vectors_view_gt(vectors_view_gt const&) noexcept = default;
    vectors_view_gt& operator=(vectors_view_gt const&) noexcept = default;

    vectors_view_gt(scalar_t const* begin, std::size_t dimensions, std::size_t count = 1) noexcept
        : vectors_view_gt(begin, dimensions, count, dimensions * sizeof(scalar_at)) {}

    vectors_view_gt(scalar_t const* begin, std::size_t dimensions, std::size_t count, std::size_t stride_bytes) noexcept
        : begin_(begin), dimensions_(dimensions), count_(count), stride_bytes_(stride_bytes) {}

    explicit operator bool() const noexcept { return begin_; }
    std::size_t size() const noexcept { return count_; }
    std::size_t dimensions() const noexcept { return dimensions_; }
    std::size_t stride() const noexcept { return stride_bytes_; }
    scalar_t const* data() const noexcept { return begin_; }
    scalar_t const* at(std::size_t i) const noexcept {
        return reinterpret_cast<scalar_t const*>(reinterpret_cast<byte_t const*>(begin_) + i * stride_bytes_);
    }
};

struct exact_offset_and_distance_t {
    u32_t offset;
    f32_t distance;
};

using exact_search_results_t = vectors_view_gt<exact_offset_and_distance_t>;

/**
 *  @brief  Helper-structure for exact search operations.
 *          Perfect if you have @b <1M vectors and @b <100 queries per call.
 *
 *  Uses a 3-step procedure to minimize:
 *  - cache-misses on vector lookups,
 *  - multi-threaded contention on concurrent writes.
 */
class exact_search_t {

    inline static bool smaller_distance(exact_offset_and_distance_t a, exact_offset_and_distance_t b) noexcept {
        return a.distance < b.distance;
    }

    using keys_and_distances_t = buffer_gt<exact_offset_and_distance_t>;
    keys_and_distances_t keys_and_distances;

  public:
    template <typename scalar_at, typename executor_at = dummy_executor_t, typename progress_at = dummy_progress_t>
    exact_search_results_t operator()(                                          //
        vectors_view_gt<scalar_at> dataset, vectors_view_gt<scalar_at> queries, //
        std::size_t wanted, metric_punned_t const& metric,                      //
        executor_at&& executor = executor_at{}, progress_at&& progress = progress_at{}) {
        return operator()(                                                                     //
            metric,                                                                            //
            reinterpret_cast<byte_t const*>(dataset.data()), dataset.size(), dataset.stride(), //
            reinterpret_cast<byte_t const*>(queries.data()), queries.size(), queries.stride(), //
            wanted, executor, progress);
    }

    template <typename executor_at = dummy_executor_t, typename progress_at = dummy_progress_t>
    exact_search_results_t operator()(                                                     //
        byte_t const* dataset_data, std::size_t dataset_count, std::size_t dataset_stride, //
        byte_t const* queries_data, std::size_t queries_count, std::size_t queries_stride, //
        std::size_t wanted, metric_punned_t const& metric, executor_at&& executor = executor_at{},
        progress_at&& progress = progress_at{}) {

        // Allocate temporary memory to store the distance matrix
        // Previous version didn't need temporary memory, but the performance was much lower.
        // In the new design we keep two buffers - original and transposed, as in-place transpositions
        // of non-rectangular matrixes is expensive.
        std::size_t tasks_count = dataset_count * queries_count;
        if (keys_and_distances.size() < tasks_count * 2)
            keys_and_distances = keys_and_distances_t(tasks_count * 2);
        if (keys_and_distances.size() < tasks_count * 2)
            return {};

        exact_offset_and_distance_t* keys_and_distances_per_dataset = keys_and_distances.data();
        exact_offset_and_distance_t* keys_and_distances_per_query = keys_and_distances_per_dataset + tasks_count;

        // §1. Compute distances in a data-parallel fashion
        std::atomic<std::size_t> processed{0};
        executor.dynamic(dataset_count, [&](std::size_t thread_idx, std::size_t dataset_idx) {
            byte_t const* dataset = dataset_data + dataset_idx * dataset_stride;
            for (std::size_t query_idx = 0; query_idx != queries_count; ++query_idx) {
                byte_t const* query = queries_data + query_idx * queries_stride;
                auto distance = metric(dataset, query);
                std::size_t task_idx = queries_count * dataset_idx + query_idx;
                keys_and_distances_per_dataset[task_idx].offset = static_cast<u32_t>(dataset_idx);
                keys_and_distances_per_dataset[task_idx].distance = static_cast<f32_t>(distance);
            }

            // It's more efficient in this case to report progress from a single thread
            processed += queries_count;
            if (thread_idx == 0)
                if (!progress(processed.load(), tasks_count))
                    return false;
            return true;
        });
        if (processed.load() != tasks_count)
            return {};

        // §2. Transpose in a single thread to avoid contention writing into the same memory buffers
        for (std::size_t query_idx = 0; query_idx != queries_count; ++query_idx) {
            for (std::size_t dataset_idx = 0; dataset_idx != dataset_count; ++dataset_idx) {
                std::size_t from_idx = queries_count * dataset_idx + query_idx;
                std::size_t to_idx = dataset_count * query_idx + dataset_idx;
                keys_and_distances_per_query[to_idx] = keys_and_distances_per_dataset[from_idx];
            }
        }

        // §3. Partial-sort every query result
        executor.fixed(queries_count, [&](std::size_t, std::size_t query_idx) {
            auto start = keys_and_distances_per_query + dataset_count * query_idx;
            if (wanted > 1) {
                // TODO: Consider alternative sorting approaches
                // radix_sort(start, start + dataset_count, wanted);
                // std::sort(start, start + dataset_count, &smaller_distance);
                std::partial_sort(start, start + wanted, start + dataset_count, &smaller_distance);
            } else {
                auto min_it = std::min_element(start, start + dataset_count, &smaller_distance);
                if (min_it != start)
                    std::swap(*min_it, *start);
            }
        });

        // At the end report the latest numbers, because the reporter thread may be finished earlier
        progress(tasks_count, tasks_count);
        return {keys_and_distances_per_query, wanted, queries_count,
                dataset_count * sizeof(exact_offset_and_distance_t)};
    }
};

/**
 *  @brief  C++11 Multi-Hash-Set with Linear Probing.
 *
 *  - Allows multiple equivalent values,
 *  - Supports transparent hashing and equality operator.
 *  - Doesn't throw exceptions, if forbidden.
 *  - Doesn't need reserving a value for deletions.
 *
 *  @section Layout
 *
 *  For every slot we store 2 extra bits for 3 possible states: empty, populated, or deleted.
 *  With linear probing the hashes at the end of the populated region will spill into its first half.
 */
template <typename element_at, typename hash_at, typename equals_at, typename allocator_at = std::allocator<char>>
class flat_hash_multi_set_gt {
  public:
    using element_t = element_at;
    using hash_t = hash_at;
    using equals_t = equals_at;
    using allocator_t = allocator_at;

    static constexpr std::size_t slots_per_bucket() { return 64; }
    static constexpr std::size_t bytes_per_bucket() {
        return slots_per_bucket() * sizeof(element_t) + sizeof(bucket_header_t);
    }

  private:
    struct bucket_header_t {
        std::uint64_t populated{};
        std::uint64_t deleted{};
    };
    char* data_ = nullptr;
    std::size_t buckets_ = 0;
    std::size_t populated_slots_ = 0;
    /// @brief  Number of slots
    std::size_t capacity_slots_ = 0;

    struct slot_ref_t {
        bucket_header_t& header;
        std::uint64_t mask;
        element_t& element;
    };

    slot_ref_t slot_ref(char* data, std::size_t slot_index) const noexcept {
        std::size_t bucket_index = slot_index / slots_per_bucket();
        std::size_t in_bucket_index = slot_index % slots_per_bucket();
        auto bucket_pointer = data + bytes_per_bucket() * bucket_index;
        auto slot_pointer = bucket_pointer + sizeof(bucket_header_t) + sizeof(element_t) * in_bucket_index;
        return {
            *reinterpret_cast<bucket_header_t*>(bucket_pointer),
            static_cast<std::uint64_t>(1ull) << in_bucket_index,
            *reinterpret_cast<element_t*>(slot_pointer),
        };
    }

    slot_ref_t slot_ref(std::size_t slot_index) const noexcept { return slot_ref(data_, slot_index); }

    bool populate_slot(slot_ref_t slot, element_t const& new_element) {
        if (slot.header.populated & slot.mask) {
            slot.element = new_element;
            slot.header.deleted &= ~slot.mask;
            return false;
        } else {
            new (&slot.element) element_t(new_element);
            slot.header.populated |= slot.mask;
            return true;
        }
    }

  public:
    std::size_t size() const noexcept { return populated_slots_; }
    std::size_t capacity() const noexcept { return capacity_slots_; }

    flat_hash_multi_set_gt() noexcept {}
    ~flat_hash_multi_set_gt() noexcept { reset(); }

    flat_hash_multi_set_gt(flat_hash_multi_set_gt const& other) {

        // On Windows allocating a zero-size array would fail
        if (!other.buckets_) {
            reset();
            return;
        }

        // Allocate new memory
        data_ = (char*)allocator_t{}.allocate(other.buckets_ * bytes_per_bucket());
        if (!data_)
            throw std::bad_alloc();

        // Copy metadata
        buckets_ = other.buckets_;
        populated_slots_ = other.populated_slots_;
        capacity_slots_ = other.capacity_slots_;

        // Initialize new buckets to empty
        std::memset(data_, 0, buckets_ * bytes_per_bucket());

        // Copy elements and bucket headers
        for (std::size_t i = 0; i < capacity_slots_; ++i) {
            slot_ref_t old_slot = other.slot_ref(i);
            if ((old_slot.header.populated & old_slot.mask) && !(old_slot.header.deleted & old_slot.mask)) {
                slot_ref_t new_slot = slot_ref(i);
                populate_slot(new_slot, old_slot.element);
            }
        }
    }

    flat_hash_multi_set_gt& operator=(flat_hash_multi_set_gt const& other) {

        // On Windows allocating a zero-size array would fail
        if (!other.buckets_) {
            reset();
            return *this;
        }

        // Handle self-assignment
        if (this == &other)
            return *this;

        // Clear existing data
        clear();
        if (data_)
            allocator_t{}.deallocate(data_, buckets_ * bytes_per_bucket());

        // Allocate new memory
        data_ = (char*)allocator_t{}.allocate(other.buckets_ * bytes_per_bucket());
        if (!data_)
            throw std::bad_alloc();

        // Copy metadata
        buckets_ = other.buckets_;
        populated_slots_ = other.populated_slots_;
        capacity_slots_ = other.capacity_slots_;

        // Initialize new buckets to empty
        std::memset(data_, 0, buckets_ * bytes_per_bucket());

        // Copy elements and bucket headers
        for (std::size_t i = 0; i < capacity_slots_; ++i) {
            slot_ref_t old_slot = other.slot_ref(i);
            if ((old_slot.header.populated & old_slot.mask) && !(old_slot.header.deleted & old_slot.mask)) {
                slot_ref_t new_slot = slot_ref(i);
                populate_slot(new_slot, old_slot.element);
            }
        }

        return *this;
    }

    void clear() noexcept {
        // Call the destructors
        for (std::size_t i = 0; i < capacity_slots_; ++i) {
            slot_ref_t slot = slot_ref(i);
            if ((slot.header.populated & slot.mask) & (~slot.header.deleted & slot.mask))
                slot.element.~element_t();
        }

        // Reset populated slots count
        if (data_)
            std::memset(data_, 0, buckets_ * bytes_per_bucket());
        populated_slots_ = 0;
    }

    void reset() noexcept {
        clear(); // Clear all elements
        if (data_)
            allocator_t{}.deallocate(data_, buckets_ * bytes_per_bucket());
        buckets_ = 0;
        populated_slots_ = 0;
        capacity_slots_ = 0;
    }

    bool try_reserve(std::size_t capacity) noexcept {
        if (capacity * 3u <= capacity_slots_ * 2u)
            return true;

        // Calculate new sizes
        std::size_t new_slots = ceil2((capacity * 3ul) / 2ul);
        std::size_t new_buckets = divide_round_up<slots_per_bucket()>(new_slots);
        new_slots = new_buckets * slots_per_bucket(); // This must be a power of two!
        std::size_t new_bytes = new_buckets * bytes_per_bucket();

        // Allocate new memory
        char* new_data = (char*)allocator_t{}.allocate(new_bytes);
        if (!new_data)
            return false;

        // Initialize new buckets to empty
        std::memset(new_data, 0, new_bytes);

        // Rehash and copy existing elements to new_data
        hash_t hasher;
        for (std::size_t i = 0; i < capacity_slots_; ++i) {
            slot_ref_t old_slot = slot_ref(i);
            if ((~old_slot.header.populated & old_slot.mask) | (old_slot.header.deleted & old_slot.mask))
                continue;

            // Rehash
            std::size_t hash_value = hasher(old_slot.element);
            std::size_t new_slot_index = hash_value & (new_slots - 1);

            // Linear probing to find an empty slot in new_data
            while (true) {
                slot_ref_t new_slot = slot_ref(new_data, new_slot_index);
                if (!(new_slot.header.populated & new_slot.mask) || (new_slot.header.deleted & new_slot.mask)) {
                    populate_slot(new_slot, std::move(old_slot.element));
                    new_slot.header.populated |= new_slot.mask;
                    break;
                }
                new_slot_index = (new_slot_index + 1) & (new_slots - 1);
            }
        }

        // Deallocate old data and update pointers and sizes
        if (data_)
            allocator_t{}.deallocate(data_, buckets_ * bytes_per_bucket());
        data_ = new_data;
        buckets_ = new_buckets;
        capacity_slots_ = new_slots;

        return true;
    }

    template <typename query_at> class equal_iterator_gt {
      public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = element_t;
        using difference_type = std::ptrdiff_t;
        using pointer = element_t*;
        using reference = element_t&;

        equal_iterator_gt(std::size_t index, flat_hash_multi_set_gt* parent, query_at const& query,
                          equals_t const& equals)
            : index_(index), parent_(parent), query_(query), equals_(equals) {}

        // Pre-increment
        equal_iterator_gt& operator++() {
            do {
                index_ = (index_ + 1) & (parent_->capacity_slots_ - 1);
            } while (!equals_(parent_->slot_ref(index_).element, query_) &&
                     (parent_->slot_ref(index_).header.populated & parent_->slot_ref(index_).mask));
            return *this;
        }

        equal_iterator_gt operator++(int) {
            equal_iterator_gt temp = *this;
            ++(*this);
            return temp;
        }

        reference operator*() { return parent_->slot_ref(index_).element; }
        pointer operator->() { return &parent_->slot_ref(index_).element; }
        bool operator!=(equal_iterator_gt const& other) const { return !(*this == other); }
        bool operator==(equal_iterator_gt const& other) const {
            return index_ == other.index_ && parent_ == other.parent_;
        }

      private:
        std::size_t index_;
        flat_hash_multi_set_gt* parent_;
        query_at query_;  // Store the query object
        equals_t equals_; // Store the equals functor
    };

    /**
     *  @brief  Returns an iterator range of all elements matching the given query.
     *
     *  Technically, the second iterator points to the first empty slot after a
     *  range of equal values and non-equal values with similar hashes.
     */
    template <typename query_at>
    std::pair<equal_iterator_gt<query_at>, equal_iterator_gt<query_at>>
    equal_range(query_at const& query) const noexcept {

        equals_t equals;
        auto this_ptr = const_cast<flat_hash_multi_set_gt*>(this);
        auto end = equal_iterator_gt<query_at>(capacity_slots_, this_ptr, query, equals);
        if (!capacity_slots_)
            return {end, end};

        hash_t hasher;
        std::size_t hash_value = hasher(query);
        std::size_t first_equal_index = hash_value & (capacity_slots_ - 1);
        std::size_t const start_index = first_equal_index;

        // Linear probing to find the first equal element
        do {
            slot_ref_t slot = slot_ref(first_equal_index);
            if (slot.header.populated & ~slot.header.deleted & slot.mask) {
                if (equals(slot.element, query))
                    break;
            }
            // Stop if we find an empty slot
            else if (~slot.header.populated & slot.mask)
                return {end, end};

            // Move to the next slot
            first_equal_index = (first_equal_index + 1) & (capacity_slots_ - 1);
        } while (first_equal_index != start_index);

        // If no matching element was found, return end iterators
        if (first_equal_index == capacity_slots_)
            return {end, end};

        // Start from the first matching element and find the end of the populated range
        std::size_t first_empty_index = first_equal_index;
        do {
            first_empty_index = (first_empty_index + 1) & (capacity_slots_ - 1);
            slot_ref_t slot = slot_ref(first_empty_index);

            // If we find an empty slot, this is our end
            if (~slot.header.populated & slot.mask)
                break;
        } while (first_empty_index != start_index);

        return {equal_iterator_gt<query_at>(first_equal_index, this_ptr, query, equals),
                equal_iterator_gt<query_at>(first_empty_index, this_ptr, query, equals)};
    }

    template <typename similar_at> bool pop_first(similar_at&& query, element_t& popped_value) noexcept {

        if (!capacity_slots_)
            return false;

        hash_t hasher;
        equals_t equals;
        std::size_t hash_value = hasher(query);
        std::size_t slot_index = hash_value & (capacity_slots_ - 1); // Assuming capacity_slots_ is a power of 2
        std::size_t start_index = slot_index;                        // To detect loop in probing

        // Linear probing to find the first match
        do {
            slot_ref_t slot = slot_ref(slot_index);
            if (slot.header.populated & slot.mask) {
                if ((~slot.header.deleted & slot.mask) && equals(slot.element, query)) {
                    // Found a match, mark as deleted
                    slot.header.deleted |= slot.mask;
                    --populated_slots_;
                    popped_value = slot.element;
                    return true; // Successfully removed
                }
            } else {
                // Stop if we find an empty slot
                break;
            }

            // Move to the next slot
            slot_index = (slot_index + 1) & (capacity_slots_ - 1); // Assuming capacity_slots_ is a power of 2
        } while (slot_index != start_index);

        return false; // No match found
    }

    template <typename similar_at> std::size_t erase(similar_at&& query) noexcept {

        if (!capacity_slots_)
            return 0;

        hash_t hasher;
        equals_t equals;
        std::size_t hash_value = hasher(query);
        std::size_t slot_index = hash_value & (capacity_slots_ - 1); // Assuming capacity_slots_ is a power of 2
        std::size_t const start_index = slot_index;                  // To detect loop in probing
        std::size_t count = 0;                                       // Count of elements removed

        // Linear probing to find all matches
        do {
            slot_ref_t slot = slot_ref(slot_index);
            if (slot.header.populated & slot.mask) {
                if ((~slot.header.deleted & slot.mask) && equals(slot.element, query)) {
                    // Found a match, mark as deleted
                    slot.header.deleted |= slot.mask;
                    --populated_slots_;
                    ++count; // Increment count of elements removed
                }
            } else {
                // Stop if we find an empty slot
                break;
            }

            // Move to the next slot
            slot_index = (slot_index + 1) & (capacity_slots_ - 1); // Assuming capacity_slots_ is a power of 2
        } while (slot_index != start_index);

        return count; // Return the number of elements removed
    }

    template <typename similar_at> element_t const* find(similar_at&& query) const noexcept {

        if (!capacity_slots_)
            return nullptr;

        hash_t hasher;
        equals_t equals;
        std::size_t hash_value = hasher(query);
        std::size_t slot_index = hash_value & (capacity_slots_ - 1); // Assuming capacity_slots_ is a power of 2
        std::size_t start_index = slot_index;                        // To detect loop in probing

        // Linear probing to find the first match
        do {
            slot_ref_t slot = slot_ref(slot_index);
            if (slot.header.populated & slot.mask) {
                if ((~slot.header.deleted & slot.mask) && equals(slot.element, query))
                    return &slot.element; // Found a match, return pointer to the element
            } else {
                // Stop if we find an empty slot
                break;
            }

            // Move to the next slot
            slot_index = (slot_index + 1) & (capacity_slots_ - 1); // Assuming capacity_slots_ is a power of 2
        } while (slot_index != start_index);

        return nullptr; // No match found
    }

    element_t const* end() const noexcept { return nullptr; }

    template <typename func_at> void for_each(func_at&& func) const {
        for (std::size_t bucket_index = 0; bucket_index < buckets_; ++bucket_index) {
            auto bucket_pointer = data_ + bytes_per_bucket() * bucket_index;
            bucket_header_t& header = *reinterpret_cast<bucket_header_t*>(bucket_pointer);
            std::uint64_t populated = header.populated;
            std::uint64_t deleted = header.deleted;

            // Iterate through slots in the bucket
            for (std::size_t in_bucket_index = 0; in_bucket_index < slots_per_bucket(); ++in_bucket_index) {
                std::uint64_t mask = std::uint64_t(1ull) << in_bucket_index;

                // Check if the slot is populated and not deleted
                if ((populated & ~deleted) & mask) {
                    auto slot_pointer = bucket_pointer + sizeof(bucket_header_t) + sizeof(element_t) * in_bucket_index;
                    element_t const& element = *reinterpret_cast<element_t const*>(slot_pointer);
                    func(element);
                }
            }
        }
    }

    template <typename similar_at> std::size_t count(similar_at&& query) const noexcept {

        if (!capacity_slots_)
            return 0;

        hash_t hasher;
        equals_t equals;
        std::size_t hash_value = hasher(query);
        std::size_t slot_index = hash_value & (capacity_slots_ - 1);
        std::size_t start_index = slot_index; // To detect loop in probing
        std::size_t count = 0;

        // Linear probing to find the range
        do {
            slot_ref_t slot = slot_ref(slot_index);
            if ((slot.header.populated & slot.mask) && (~slot.header.deleted & slot.mask)) {
                if (equals(slot.element, query))
                    ++count;
            } else if (~slot.header.populated & slot.mask) {
                // Stop if we find an empty slot
                break;
            }

            // Move to the next slot
            slot_index = (slot_index + 1) & (capacity_slots_ - 1);
        } while (slot_index != start_index);

        return count;
    }

    template <typename similar_at> bool contains(similar_at&& query) const noexcept {

        if (!capacity_slots_)
            return false;

        hash_t hasher;
        equals_t equals;
        std::size_t hash_value = hasher(query);
        std::size_t slot_index = hash_value & (capacity_slots_ - 1);
        std::size_t start_index = slot_index; // To detect loop in probing

        // Linear probing to find the first match
        do {
            slot_ref_t slot = slot_ref(slot_index);
            if (slot.header.populated & slot.mask) {
                if ((~slot.header.deleted & slot.mask) && equals(slot.element, query))
                    return true; // Found a match, exit early
            } else
                // Stop if we find an empty slot
                break;

            // Move to the next slot
            slot_index = (slot_index + 1) & (capacity_slots_ - 1);
        } while (slot_index != start_index);

        return false; // No match found
    }

    void reserve(std::size_t capacity) {
        if (!try_reserve(capacity))
            throw std::bad_alloc();
    }

    bool try_emplace(element_t const& element) noexcept {
        // Check if we need to resize
        if (populated_slots_ * 3u >= capacity_slots_ * 2u)
            if (!try_reserve(populated_slots_ + 1))
                return false;

        hash_t hasher;
        std::size_t hash_value = hasher(element);
        std::size_t slot_index = hash_value & (capacity_slots_ - 1);

        // Linear probing
        while (true) {
            slot_ref_t slot = slot_ref(slot_index);
            if ((~slot.header.populated & slot.mask) | (slot.header.deleted & slot.mask)) {
                // Found an empty or deleted slot
                populate_slot(slot, element);
                ++populated_slots_;
                return true;
            }
            // Move to the next slot
            slot_index = (slot_index + 1) & (capacity_slots_ - 1);
        }
    }
};

} // namespace usearch
} // namespace unum
