---
title: YugabyteDB coding style
headerTitle: YugabyteDB coding style
linkTitle: Coding style
description: YugabyteDB coding style
headcontent: YugabyteDB coding style
menu:
  preview:
    identifier: coding-style
    parent: core-database
    weight: 2914
type: docs
---

YugabyteDB is primarily written in C++ (the distributed storage and transactions layer and the YCQL query layer) and C (the YSQL layer based on PostgreSQL), with some parts of the build system and test suite written in Python, Java, and Bash. In addition, we use [Protocol Buffers](https://developers.google.com/protocol-buffers) to define some data and network message formats.

## Language-agnostic style guidelines

### Variable names

Avoid rarely used abbreviations. Think about whether all other potential readers of the code know about any abbreviations you're using.

### Comments

Start full sentences with a capital letter, and end them with a period. This rule doesn't apply if the comment is a single phrase on the same line as a code statement.

Functions and classes in header files should typically have detailed comments, but the comments shouldn't duplicate what's already obvious from the code. In fact, if the code can be restructured, or if functions or classes could be renamed to reduce the need for comments, that is the preferred way. Obvious comments like the following don't add anything useful:

```cpp
  // Returns transaction ID.
  const TransactionId& id() const;
```

Functions and classes in `.cc` files don't have to be commented as extensively as code in header files. However, do add comments and examples for anything that might be non-obvious to a potential new reader of your code.

## C coding style

For the modified PostgreSQL C codebase residing inside the YugabyteDB codebase, we adhere to the [PostgreSQL Coding Conventions](https://www.postgresql.org/docs/13/source-format.html).

Note that PostgreSQL code uses _tabs_ for indentation and we follow that rule in the [`src/postgres`](https://github.com/yugabyte/yugabyte-db/tree/master/src/postgres) subdirectory; we use spaces for indentation _everywhere else in YugabyteDB code_.

## C++ coding style

### Line length

Use a 100-character line length limit.

### Formatting function declarations and definitions

Use one of the following formatting styles for function declarations and definitions.

#### All arguments on one line

```cpp
ReturnType ClassName::FunctionName(ParameterType1 par_name1, ParameterType2 par_name2) {
  DoSomething();
  ...
```

#### Aligned, one argument per line

All arguments aligned with the opening parenthesis, one argument per line:

```cpp
ReturnType ShortClassName::ShortFunctionName(ParameterType1 par_name1,
                                             ParameterType2 par_name2,
                                             ParameterType3 par_name3) {
  DoSomething();  // 2-space indentation
  ...
}
```

#### Four-space indentation

One argument per line, with four-space indentation for each argument:

```cpp
ReturnType SomeClassName::ReallyLongFunctionName(
    ParameterType1 par_name1,  // 4-space indentation
    ParameterType2 par_name2,
    ParameterType3 par_name3) {
  DoSomething();  // 2-space indentation
  ...
}
```

#### Packed

Pack arguments into the fewest number of lines&mdash;but not exceeding the maximum line width&mdash;with each line indented by four spaces.

Don't break the argument list arbitrarily, and only break the list if the next argument won't fit within the line-length limit:

```cpp
// Suppose this is the right margin -----------------------------------------------------------> |
//                                                                                               |
//                                                                                               |

ReturnType SomeClassName::ReallyLongFunctionName(
    ParameterType1 par_name1, ParameterType2 par_name2, ParameterType3 par_name3,
    ParameterType4 par_name4, ParameterType1 par_name5) {  // 4-space indentation for these 2 lines
  DoSomething();  // 2-space indentation
  ...
}
```

### Formatting function calls and macro invocations

Use one of the following formatting styles for functions calls and macro invocations.

#### All arguments on one line {#func-call-all-args-one-line}

```cpp
bool result = DoSomething(argument1, argument2, argument3);
```

#### Aligned arguments {#func-call-aligned-args}

All arguments aligned with the opening parenthesis, one argument per line:

```cpp
bool result = DoSomething(very_very_very_very_long_argument,
                          argument2,
                          argument3);
```

#### One argument per line, four-space indentation

```cpp
bool result = DoSomething(
    argument1,  // 4-space indentation
    argument2,
    argument3);
```

#### Packed arguments, four-space indentation, wrapping at margin

Start a new line after the opening parenthesis, with a four-space indentation, and pack arguments into as few lines as possible:

```cpp
// Suppose this is the right margin -----------------------------------------------------------> |
//                                                                                               |
//                                                                                               |

bool result = DoSomething(
    argument1, argument2, argument3, argument4, argument5, argument6, argument7, argument8,
    argument9, argument10, argument11, argument12, argument13, argument14, argument15,
    argument16, argument17);
```

#### Function calls within function calls

The preceding formatting styles also apply to nested function calls:

```cpp
bool result = DoSomething(
    argument1,
    argument2,
    ReallyLongFunctionName(
        ReallyLongArg1,
        arg2,
        arg3),
    argument3,
    argument4);
```

### String substitution functions

For string substitution and formatting functions (`Format`, `Substitute`, `StringPrintf`, and so on) avoid putting substitution parameters on the same line as the format string, unless the entire function call fits on one line:

```cpp
// Suppose this is the right margin -----------------------------------------------------------> |
//                                                                                               |
//                                                                                               |

// Good:
return Format(
    "My formatting string with arguments $0, $1, $2, $3, and $4",
    compute_arg0(), compute_arg1(), compute_arg2(), compute_arg3(), compute_arg4());

// Bad: notice it's harder to see where the first substitution argument is.
return Format(
    "My formatting string with arguments $0, $1, $2, $3, and $4", compute_arg0(),
    compute_arg1(), compute_arg2(), compute_arg3(), compute_arg4());
```

### Expressions

Indent multi-line expressions as follows:

```cpp
const bool is_fixed_point_get = !lower_doc_key.empty() &&
                                upper_doc_key.HashedComponentsEqual(lower_doc_key);
```

Or:

```cpp
const bool is_fixed_point_get =
    !lower_doc_key.empty() &&
    upper_doc_key.HashedComponentsEqual(lower_doc_key);
```

The following style is also [widely used](https://gist.github.com/ttyusupov/fea3736f0265c11c4b1e8bc4d1e69f93) in the YugabyteDB codebase, so it's acceptable to leave it as-is when modifying the surrounding code, but the two previous options are preferable for new code:

```cpp
const bool is_fixed_point_get = !lower_doc_key.empty() &&
    upper_doc_key.HashedComponentsEqual(lower_doc_key);
const auto mode = is_fixed_point_get ? BloomFilterMode::USE_BLOOM_FILTER :
    BloomFilterMode::DONT_USE_BLOOM_FILTER;
```

#### Ternary operator

For expressions involving the ternary operator (`?` and `:`), prefer one of the following formatting styles:

```cpp
const auto mode = is_fixed_point_get ? BloomFilterMode::USE_BLOOM_FILTER
                                     : BloomFilterMode::DONT_USE_BLOOM_FILTER;
```

Or:

```cpp
const auto mode =
    is_fixed_point_get ? BloomFilterMode::USE_BLOOM_FILTER
                       : BloomFilterMode::DONT_USE_BLOOM_FILTER;
```

### Command-line flag definitions

Put the flag name on the same line as `DEFINE_...` to make the code more grep-friendly.

Follow function-like macro invocation styles with either [all arguments on one line]({{< relref "#func-call-all-args-one-line" >}}) or with [aligned arguments]({{< relref "#func-call-aligned-args" >}}) as defined earlier, depending on whether all three arguments of the flag definition fit on one line:

```cpp
DEFINE_bool(create_table,
            true,
            "Whether the table should be created. It's made false when either "
            "reads_only/writes_only is true. If value is true, existing table will be deleted and "
            "recreated.");
```

Note that in this coding style, the first line of the string constant (the flag description) is aligned with its other lines. Some coding styles, including CLion's standard behavior, indent the second and later lines by four spaces, but the Yugabyte preference is to keep them aligned.

### Forward declarations

You can use forward declarations in a header file, if the class you are referencing is to be defined in the related `.cc` file, essentially making it a private class implementation. This helps keep the header file cleaner, and keeps the definition and implementation of the private class together with the actual implementation of the main class.

Special header files named `..._fwd.h` that forward-declare various classes and declare some types, including enums, are frequently used. This helps to avoid including full class declarations wherever possible, and reduces the compilation time.

### Parameter ordering

Typically, in function parameter lists in YugabyteDB, input parameters appear first, followed by output parameters.

However, this rule doesn't apply to parameters that aren't pointers to variables to be assigned to by the functions, but are simply non-const objects that the function calls some methods on that modify its state.

For example, in the following function it is OK that `writer` is in the middle of the parameter list, because the function is not calling the assignment operator on `*writer` but simply calls some member functions of `*writer` that modify its internal state, even though this is how this particular function produces its output:

```cpp
void Tablet::EmitRocksDBMetrics(std::shared_ptr<rocksdb::Statistics> rocksdb_statistics,
                                JsonWriter* writer,
                                const MetricJsonOptions& opts) {
```

### Pointer and reference parameters and variables

Put a space on either side of `*` and `&` (but not on both sides).

Both of the following examples are correct:

```cpp
  Status GetSockAddrorTS(const std::string& ts_uuid, Sockaddr* ts_addr);
```

```cpp
  Status GetSockAddrForTS(const std::string &ts_uuid, Sockaddr *ts_addr);
```

Similarly, for variable declarations and definitions:

```cpp
  int* a = nullptr;
```

```cpp
  int *a = nullptr;
```

### Pointer vs reference as returned value

When a function never returns `nullptr`, prefer returning references instead of pointers.

This has the following advantages:

* It's pretty clear to the user of the function that it would never return `nullptr`, and they don't have to check for it.
* It works perfectly with YugabyteDB policy to pass read-write arguments as pointers, because the caller will add `&` while passing the result of such function to another call:

    ```cpp
    DoSomething(..., &GetX());
    ```

* It's clear that the caller doesn't own the returned reference. We can also avoid cases where ownership is taken by mistake.

    Consider the following examples, where `GetP()` returns pointer and `GetR()` returns reference:

    ```cpp
    // No compilation error. It is useful for refactoring.
    std::unique_ptr<P> p = GetP();
    ```

    ```cpp
    auto p = GetP();
    p->foo(); // It's unclear for the caller whether p is stored as a smart pointer and
              // owned by the caller, or returned as raw pointer and owned by someone else.
    ...
    auto& r = GetR(); // Would not even compile without &, so it's pretty clear
                      // that r is reference and not owned by the caller.
    r.foo();
    ```

### Get prefix for getters

Some C++ coding styles (such as Google's) use the `Get` prefix for functions returning a value, and some don't (Boost, STL). In YugabyteDB code it is allowed to either use or not use the `Get` prefix.

There are [many](https://gist.githubusercontent.com/mbautin/97c509b3b0ec206d87cdf5a225faa515/raw) different function names in YugabyteDB codebase with the `Get` prefix.

The `Get` prefix is especially useful if the function name could be misinterpreted as a verb or a verb phrase without it, but is optional in all cases.

If you can't decide whether to use the `Get` prefix for functions, look at other functions in the same class, file, or subsystem, and adhere to the prevalent naming style in the surrounding code.

### Code duplication

Try to reduce code duplication by extracting repeated code into reusable functions, classes, or templates. Reducing code duplication with macros is also allowed, but try not to overuse them; use non-macro-based ways to reuse code duplication as much as possible. Undefine macros that are only used in an isolated section of code when they're no longer needed.

### Switch statements over enums

If you don't use the default statement in a switch over an enum, the compiler will warn you if some values aren't handled (and that warning has been made into an error). This allows the compiler to enforce that all enum values are being handled by a switch over an enum, if that's your intention. This complicates default case handling a bit, though. If every case is followed by a return, you can simply move the default handler to right after the end of the switch statement, such as:

```cpp
switch (operation_type) {
  case OperationType::kInsert:
    ...
    return;
  case OperationType::kDelete:
    ...
    return;
}
FATAL_INVALID_ENUM_VALUE(OperationType, operation_type);
```

In this case, when you add a new request type, such as `OperationType::kUpdate`, you'll get a compile error asking you to add it to the switch statement. If there is no return following each case handler, the default case by (for example) setting a boolean flag can be detected:

```cpp
boolean handled = false;
switch (operation_type) {
  case OperationType::kInsert:
    ...
    handled = true;
    break;
  case OperationType::kDelete:
    ...
    handled = true;
    break;
}
if (!handled) {
  FATAL_INVALID_ENUM_VALUE(OperationType, operation_type);
}
```

Note that `FATAL_INVALID_ENUM_VALUE` will terminate the process, so for functions returning a `Status`, you should handle invalid enum values gracefully:

```cpp
return STATUS_FORMAT(
    IllegalArgument, "Invalid value of operation type: $0",
    operation_type);
```

<!--

### Arguments passed by value

**Don't** use `const` in function (including member function) declarations when the argument is passed by value.

**Do** use `const` in function (including member function) definitions wherever appropriate to indicate (and enforce) that the object is not modified inside the function body.

In `widget.h`:

```cpp
class Widget {
 public:
  void Say(Phrase p);
};
```

In `widget.cc`:

```cpp
void Widget::Say(const Phrase p) {
  . . .
}
```

Or in `widget.h`:

```cpp
void ProcessWidget(WidgetType widget_type,
                   int widget_cost,
                   const Widget& widget);
```

And in `widget.cc`:

```cpp
void ProcessWidget(const WidgetType widget_type,
                   const int widget_cost.
                   const Widget& widget) {
  // Implementation
}
```

Here's an example of what **not** to do (in `widget.h`):

```cpp
void ProcessWidget(const WidgetType widget_type,  // BAD: "const" should be removed!
                   const int widget_cost.         // BAD: "const" should be removed!
                   const Widget& widget);         // OK: "const" is part of const reference.
```

-->

### The using keyword and namespaces

You can use the `using` directive for inner utility namespaces in `.cc` files (but not in header files):

```cpp
using namespace std::placeholders;
using namespace std::literals;
using namespace yb::size_literals;
```

This allows you to use various convenient literals, such as `100ms` to denote "100 milliseconds".

### Unnamed namespaces vs static keyword

When you define functions that should only be accessible within a `.cc` file, prefer using unnamed namespaces to using the `static` keyword. See for example the [Unnamed/anonymous namespaces vs. static functions](https://stackoverflow.com/questions/154469/unnamed-anonymous-namespaces-vs-static-functions) discussion.

### Static and global variables

Using static objects is allowed in YugabyteDB coding style with a few limitations, and with an understanding of a static object's lifecycle. The limitations are:

* Static global variables **must** be totally independent. The order of constructions/destructions should not matter.
* Static objects **must not** lock anything or allocate global resources (except memory) during their construction.
* Static objects should be as simple as possible.

If you are adding new static objects, remember:

* Global static objects (as well as static class fields) are constructed before the `main()` function call.
* Local static objects are constructed when they first come into variable scope. This option should be preferred. For local static objects, there is also a [locking mechanism](http://stackoverflow.com/questions/8102125/is-local-static-variable-initialization-thread-safe-in-c11) needed so that a concurrent execution waits for initialization completion if the static variable is already being initialized by another thread. For example, for a simple static variable defined in a function:

    ```cpp
    void f() {
        static string s = "asdf";
    }
    ```

    The generated code might be using a lock automatically to guard the static variable initialization:

    ```asm
      call    __cxa_guard_acquire
            test    eax, eax
            setne   al
            test    al, al
            je      .L6
            mov     ebx, 0
            lea     rax, [rbp-17]
            mov     rdi, rax
            call    std::allocator<char>::allocator()
            lea     rax, [rbp-17]
            mov     rdx, rax
            mov     esi, OFFSET FLAT:.LC0
            mov     edi, OFFSET FLAT:f()::s
            call    std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::basic_string(char const*, std::allocator<char> const&)
            mov     edi, OFFSET FLAT:guard variable for f()::s
            call    __cxa_guard_release
    ```

* Static objects are destroyed after the end of the `main()` function.
* The order of global static objects construction is undefined, and can even change from build to build.
* The order of static objects destructions is reverse of the order of constructions, so it is also undefined.

### Multiple inheritance

Use protected or private inheritance if you can.

Multiple inheritance is allowed in general, subject to a few limitations:

* Don't down-cast pointers or references (casting from a base to a derived class).

    Note that the only way to do this safely in C++ is using the `dynamic_cast<...>` operator, which relies on run-time type information (RTTI), and there is a possibility that RTTI might be disabled in release builds at some point for performance reasons.

* Don't use multiple inheritance if a base class is part of this object from an architectural point of view. Instead, prefer composition (making the "part" object a member field in the "whole" object) in such cases.

### Testing whether a pointer is null

It's OK to use implicit boolean conversion for raw, unique, or shared pointers:

```cpp
if (p) {
  p->DoSomething();
} else {
  ...  // Handle the nullptr case
}
```

Another example:

```cpp
LOG(INFO) << (p ? p->DebugString() : "N/A");
```

### Error checking macros

Various macros are used for error and invariant checking.

#### Checking a condition and returning a Status with SCHECK {#scheck}

`SCHECK` (stands for "Status check") checks a condition and returns a `Status` with the appropriate message if the condition is not true. It can only be used within a function that returns a `Status` or a `Result`.

The `SCHECK` macro is a good way to check for errors that are expected to happen under some conditions, for example, with invalid input, and the errors need to be ultimately reported to the user:

```cpp
SCHECK(key_opt.is_initialized(), InternalError, "Key is not initialized");
```

There are also variants the `SCHECK` macro for equality checks and comparisons: `SCHECK_EQ`, `SCHECK_NE`, `SCHECK_GT`, `SCHECK_LT`, `SCHECK_GE`, and `SCHECK_LE`:

```cpp
SCHECK_EQ(schedules.size(), 1, IllegalState,
          Format("Expected exactly one schedule with id $0", schedule_id));
```

#### Returning a Status in release mode, but triggering a fatal error in debug mode, with RSTATUS_DCHECK {#rstatus_dcheck}

`RSTATUS_DCHECK` works similarly to `SCHECK` in release mode, but triggers a fatal error and a log message in debug mode, terminating program execution. Similarly to `SCHECK`, it also has variants for checking for equality and inequality. `RSTATUS_DCHECK` can be used for invariant checks and sanity checks where the error is not expected to happen under normal circumstances but there is a recovery path from this error in a production situation. In these cases it is OK to cause a unit test to crash in debug mode when the error is encountered.

#### Checking an invariant that must always hold with CHECK {#check}

For really important invariants from which it is difficult to recover while still maintaining correctness, sometimes the `CHECK` macro and its variants are used. It is enabled in both debug and release modes and causes program termination if the condition is not true. It should be used really carefully to avoid causing unnecessary server restarts in release mode.

#### Only checking a condition in debug mode with DCHECK

This macro expands to a no-op in release mode. This is reserved for checking invariants or preconditions in performance-critical code, and typically only in cases when the condition is expected to be true because other code guarantees it.

Sometimes `DCHECK` is used to verify function prerequisites. If you never expect an incorrect parameter value to be passed into a function, because there is validation happening in the calling function, it's OK to keep that as a `DCHECK`.

However, if you could theoretically get bad data in production at a certain point in the code, then:

* If you can recover from this error, return an error `Status` (e.g. using [`SCHECK`]({{< relref "#scheck" >}}) or [`RSTATUS_DCHECK`]({{< relref "#rstatus_dcheck" >}})).
* If this is a severe invariant violation and you can't recover from it, this could be a [`CHECK`]({{< relref "#check" >}}).

### Log levels

Use the following policy when choosing log levels:

* `INFO` - general logs that describe some important actions taken by the system.

    This should not be too verbose, that is, avoid logging the same information several times per second. Use `YB_LOG_EVERY_N_SECS` when necessary.

* `WARNING` - expected failures. Failures that could occur while service is running. For instance, network disconnect, timeout, and so on.
* `ERROR` - should NEVER be used.
* `DFATAL` - unexpected failures. Some sanity check failures that should not happen if the system operates correctly. For example, unexpected state, data corruption, and so on.

    `DFATAL` logs an `ERROR` in release mode and `FATAL` in debug mode. So, tests will fail when such issues happen.

    Use `RSTATUS_DCHECK` whenever possible, instead of `DFATAL`.

### PREDICT_TRUE and PREDICT_FALSE

`PREDICT_TRUE` and `PREDICT_FALSE` macros expand to hints to the compiler that a particular code path is likely or unlikely. In theory, these macros could allow better compiler optimizations. However, we don't use these macros in new code, as it's difficult to check if they really improve performance.

### Result vs Status with output parameters

For new code, use [`Result`](https://github.com/yugabyte/yugabyte-db/blob/master/src/yb/util/result.h), for example:

```cpp
static Result<DocKeyHash> DecodeHash(const Slice& slice);
```

Much of YugabyteDB code is wired to return `Status`, in order to provide the information on whether or not a function completed successfully (OK). However, sometimes it is useful to obtain legitimate output from these functions. This used to be done via function signatures such as the following:

```cpp
// Old approach, don't use this in new code.
Status foo(int* return_variable);
```

However, the `Result` type is a better way to achieve the same goal, because it can encapsulate both an output parameter (in the successful case) and a `Status` (in case of an error):

```cpp
Result<int> foo();
```

### String formatting

Use the `Format` function to produce formatted strings, rather than the older `Substitute` function.

While the two functions have similar syntax, with inline substitution parameters `$0`, `$1`, and so on, `Format` has several advantages:

* It uses the `ToString` utility, so it can convert many different types of objects to strings, such as collections, protobufs, or any class with a `ToString` member function.
* You don't need to call `arg.ToString()`. Just pass `arg` to the `Format` function as-is, and it will call `ToString` for you.
* `Format` is a bit faster than `Substitute` on some benchmarks.

### consensus::OpId

`consensus::OpId` is just an alias for `yb::OpIdPB`, a protobuf class. Use this _only_ where you really need to use protobuf, for example inside other protobuf messages. For the rest of the code, use `yb::OpId`, a normal C++ class.

### CHECKED_STATUS

Avoid using `CHECKED_STATUS`; use `Status` for function return type in all new code.

The YugabyteDB codebase used to contain a lot of `CHECKED_STATUS` usage. You might still encounter it in older release branches. It was a useful macro when the YugabyteDB main compiler did not support the [[nodiscard]] class attribute, but after switching to a modern compiler, this macro is no longer necessary.

### Thread safety analysis

Clang's [Thread Safety Analysis](https://clang.llvm.org/docs/ThreadSafetyAnalysis.html) annotations are used in parts of YugabyteDB code.

Thread safety analysis is a C++ extension implemented as part of [Clang Static Analyzer](https://clang-analyzer.llvm.org/) that provides compile-time checks for potential race conditions in code. Annotations are extremely useful for code maintainability, because they make locking semantics explicit, and the compiler warns about accessing memory without holding the necessary mutexes.

A few more things to keep in mind:

* `std::unique_lock` doesn't work with thread safety annotations out of the box. This is wrapped into YugabyteDB custom `yb::UniqueLock` wrapper.
* Similarly, there is the `yb::SharedLock` wrapper for `std::shared_lock`.
* Occasionally, you may need to annotate some functions where thread safety analysis cannot be properly applied, with `NO_THREAD_SAFETY_ANALYSIS`, so that thread safety analysis can still be used in the surrounding code. The situations in which thread safety analysis might not work include conditional locking and complex locking semantics where a unique lock is being passed around between member functions of different classes.

YugabyteDB build scripts enable thread safety analysis for Clang version 11 and later; earlier versions of the Clang compiler don't support certain features required by YugabyteDB. Thread safety analysis works very well on macOS with modern Clang compilers, providing instant hints if your environment is set up properly.

### Unused C and C++ features

* C++ exceptions: We don't use C++ exceptions in most of the YugabyteDB code. However, in some cases, we use C++ standard library functions that throw exceptions; we catch those exceptions as early as possible, and convert them into `Status` or `Result` return values.
* `assert`: C library macro. YugabyteDB uses its own set of macros for invariant checking.

### Other related coding guidelines for C++

In addition to the guidelines outlined on this page, the YugabyteDB C++ coding style is based on [Google's C++ style guide](https://google.github.io/styleguide/cppguide.html).

## Protocol buffers coding style

Name protobuf structures and enums with a `PB` suffix. This is for the following reasons:

* Better "greppability" (searchability) means fewer false positive results when searching for `TableTypePB` and not `TableType`.
* It's possible to have structs and classes with the same name, but without PB suffix for usage in the code.
* It's possible to have enum wrappers similar to `YB_DEFINE_ENUM` with the same name as the protobuf enum, but without the PB suffix.
