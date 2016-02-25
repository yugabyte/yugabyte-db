// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#ifndef KUDU_CLIENT_STUBS_H
#define KUDU_CLIENT_STUBS_H

#include <stdlib.h> // for exit()

#include <iostream>

//
// GCC can be told that a certain branch is not likely to be taken (for
// instance, a CHECK failure), and use that information in static analysis.
// Giving it this information can help it optimize for the common case in
// the absence of better information (ie. -fprofile-arcs).
//
#ifndef PREDICT_FALSE
#if defined(__GNUC__)
#define PREDICT_FALSE(x) (__builtin_expect(x, 0))
#else
#define PREDICT_FALSE(x) x
#endif
#endif
#ifndef PREDICT_TRUE
#if defined(__GNUC__)
#define PREDICT_TRUE(x) (__builtin_expect(!!(x), 1))
#else
#define PREDICT_TRUE(x) x
#endif
#endif

// Annotate a function indicating the caller must examine the return value.
// Use like:
//   int foo() WARN_UNUSED_RESULT;
// To explicitly ignore a result, see |ignore_result()| in <base/basictypes.h>.
#ifndef WARN_UNUSED_RESULT
#if defined(__GNUC__)
#define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define WARN_UNUSED_RESULT
#endif
#endif

#if (defined(__GNUC__) || defined(__APPLE__)) && !defined(SWIG)
#undef ATTRIBUTE_UNUSED
#define ATTRIBUTE_UNUSED __attribute__ ((unused))
#else
#ifndef ATTRIBUTE_UNUSED
#define ATTRIBUTE_UNUSED
#endif
#endif

#ifndef COMPILE_ASSERT
// The COMPILE_ASSERT macro can be used to verify that a compile time
// expression is true. For example, you could use it to verify the
// size of a static array:
//
//   COMPILE_ASSERT(ARRAYSIZE(content_type_names) == CONTENT_NUM_TYPES,
//                  content_type_names_incorrect_size);
//
// or to make sure a struct is smaller than a certain size:
//
//   COMPILE_ASSERT(sizeof(foo) < 128, foo_too_large);
//
// The second argument to the macro is the name of the variable. If
// the expression is false, most compilers will issue a warning/error
// containing the name of the variable.

template <bool>
struct StubsCompileAssert {
};

#define COMPILE_ASSERT(expr, msg) \
  typedef StubsCompileAssert<(bool(expr))> msg[bool(expr) ? 1 : -1] ATTRIBUTE_UNUSED // NOLINT(*)
#endif

// Annotate a virtual method indicating it must be overriding a virtual
// method in the parent class.
// Use like:
//   virtual void foo() OVERRIDE;
#ifndef OVERRIDE
#if defined(COMPILER_MSVC)
#define OVERRIDE override
#elif defined(__clang__)
#define OVERRIDE override
#elif defined(COMPILER_GCC) && __cplusplus >= 201103 && \
      (__GNUC__ * 10000 + __GNUC_MINOR__ * 100) >= 40700
// GCC 4.7 supports explicit virtual overrides when C++11 support is enabled.
#define OVERRIDE override
#else
#define OVERRIDE
#endif
#endif

#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);               \
  void operator=(const TypeName&)
#endif

#ifndef FRIEND_TEST
#define FRIEND_TEST(test_case_name, test_name) \
  friend class test_case_name##_##test_name##_Test
#endif

// Stubbed versions of macros defined in glog/logging.h, intended for
// environments where glog headers aren't available.
//
// Add more as needed.

#define KUDU_DCHECK(condition) while (false) kudu::internal_logging::NullLog()
#define KUDU_DCHECK_EQ(val1, val2) while (false) kudu::internal_logging::NullLog()
#define KUDU_DCHECK_NE(val1, val2) while (false) kudu::internal_logging::NullLog()
#define KUDU_DCHECK_LE(val1, val2) while (false) kudu::internal_logging::NullLog()
#define KUDU_DCHECK_LT(val1, val2) while (false) kudu::internal_logging::NullLog()
#define KUDU_DCHECK_GE(val1, val2) while (false) kudu::internal_logging::NullLog()
#define KUDU_DCHECK_GT(val1, val2) while (false) kudu::internal_logging::NullLog()
#define KUDU_DCHECK_NOTNULL(val) (val)
#define KUDU_DCHECK_STREQ(str1, str2) while (false) kudu::internal_logging::NullLog()
#define KUDU_DCHECK_STRCASEEQ(str1, str2) while (false) kudu::internal_logging::NullLog()
#define KUDU_DCHECK_STRNE(str1, str2) while (false) kudu::internal_logging::NullLog()
#define KUDU_DCHECK_STRCASENE(str1, str2) while (false) kudu::internal_logging::NullLog()

// Log levels. LOG ignores them, so their values are abitrary.

#define KUDU_INFO 0
#define KUDU_WARNING 1
#define KUDU_ERROR 2
#define KUDU_FATAL 3

#ifdef NDEBUG
#define KUDU_DFATAL KUDU_WARNING
#else
#define KUDU_DFATAL KUDU_FATAL
#endif // NDEBUG

#define KUDU_LOG_INTERNAL(level) kudu::internal_logging::CerrLog(level)
#define KUDU_LOG(level) KUDU_LOG_INTERNAL(KUDU_##level)

#define KUDU_CHECK(condition) \
  (condition) ? 0 : KUDU_LOG(FATAL) << "Check failed: " #condition " "

namespace kudu {

namespace internal_logging {

class NullLog {
 public:
  template<class T>
  NullLog& operator<<(const T& t) {
    return *this;
  }
};

class CerrLog {
 public:
  CerrLog(int severity) // NOLINT(runtime/explicit)
    : severity_(severity),
      has_logged_(false) {
  }

  ~CerrLog() {
    if (has_logged_) {
      std::cerr << std::endl;
    }
    if (severity_ == KUDU_FATAL) {
      exit(1);
    }
  }

  template<class T>
  CerrLog& operator<<(const T& t) {
    has_logged_ = true;
    std::cerr << t;
    return *this;
  }

 private:
  const int severity_;
  bool has_logged_;
};

} // namespace internal_logging
} // namespace kudu

#endif
