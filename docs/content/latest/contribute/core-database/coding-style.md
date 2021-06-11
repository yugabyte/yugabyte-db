---
title: YugabyteDB coding style
headerTitle: YugabyteDB coding style
linkTitle: Coding style
description: YugabyteDB coding style
image: /images/section_icons/index/quick_start.png
headcontent: YugabyteDB coding style
type: page
menu:
  latest:
    identifier: coding-style
    parent: core-database
    weight: 2914
isTocNested: true
showAsideToc: true
---

YugabyteDB is primarily written in C++, with some parts of the build system and test suite written in Python, Java, and Bash (to be replaced with Python).
We also use [Protocol Buffers](https://developers.google.com/protocol-buffers) to define some data and network message formats.

## Language-agnostic coding style

### Variable names

Avoid rarely used abbreviations. Think about whether all other potential readers of the code know about the abbreviation you are about to use.

### Comments

When writing full sentences, start them with a capital letter and end them with a period. This rule does not apply if the comment is just a single phrase on the same line with a code statement.

## C coding style
For the modified PostgreSQL C codebase residing inside the YugabyteDB codebase we adhere to [PostgreSQL Coding Conventions](https://www.postgresql.org/docs/13/source-format.html).
Note that PostgreSQL code uses tabs for indentation, but we use spaces for indentation everywhere else in YugabyteDB code.

## C++ coding style

### Line length

Use 100-character line length limit instead of 80-character.

### Formatting function declarations and definitions

Use one of the following formatting styles for function declarations and definitions.

#### All arguments on one line

```cpp
ReturnType ClassName::FunctionName(Type par_name1, Type par_name2) {
  DoSomething();
  ...
```

#### Aligned with one argument per line

All arguments aligned with the opening parenthesis, one argument per line.

```cpp
ReturnType ShortClassName::ShortFunctionName(Type par_name1,
                                             Type par_name2,
                                             Type par_name3) {
  DoSomething();  // 2-space indentation
  ...
}
```

#### Four-space indentation

One argument per line with four space indentation for each argument.

```cpp
ReturnType SomeClassName::ReallyLongFunctionName(
    Type par_name1,  // 4-space indentation
    Type par_name2,
    Type par_name3) {
  DoSomething();  // 2-space indentation
  ...
}
```

#### Packed

Arguments are packed into the fewest number of lines not exceeding the maximum line width, with four-space indentation for each line.
Do not break the argument list arbitrarily, but only break it when the next argument would not fit into the line length limit if left on the same line as the previous argument.

```cpp
ReturnType SomeClassName::ReallyLongFunctionName(
    Type par_name1, Type par_name2, Type par_name3, Type par_name4, Type par_name5,
    Type par_name6, Type par_name7) {
  DoSomething();  // 2-space indentation
  ...
}
```

### Function calls and macro invocations

Use one of the formatting styles below for functions calls and macro invocations:

#### All arguments on one line {#func-call-all-args-one-line}

```cpp
bool result = DoSomething(argument1, argument2, argument3);
```

#### Aligned arguments {#func-call-aligned-args}

All arguments aligned with the opening parenthesis, one argument per line.

```cpp
bool result = DoSomething(very_very_very_very_long_argument,
                          argument2,
                          argument3);
```

#### One argument per line with four-space indentation

```cpp
    bool result = DoSomething(
        argument1,  // 4-space indentation
        argument2,
        argument3);
```

#### Packed arguments, four-space indentation, wrapping at margin

Start a new line after the opening parenthesis, with a four-space indentation, and pack arguments into as few lines as possible.

```
    bool result = DoSomething(
        argument1, argument2, argument3, argument4, argument5, argument6, argument7, argument8,
        argument9, argument10, argument11, argument12, argument13, argument14, argument15,
        argument16, argument17);
```

#### Function calls within function calls

The above formatting styles apply naturally to function calls appearing within function calls.

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

For string substitution and formatting functions (`Format`, `Substitute`, `StringPrintf`, etc.) it is better to avoid putting substitution parameters on the same line as the format string, unless the entire function call fits on one line.
Prefer the following style for multi-line substitution/formatting function calls:

```cpp
  // Suppose this is the right margin -----------------------------------------------------------> |
  //                                                                                               |
  //                                                                                               |

  // Good:
  return Substitute(
      "My formatting string with arguments $0, $1, $2, $3, and $4",
      compute_arg0(), compute_arg1(), compute_arg2(), compute_arg3(), compute_arg4());

  // Bad: notice it is harder to see where the first substitution argument is.
  return Substitute(
      "My formatting string with arguments $0, $1, $2, $3, and $4", compute_arg0(),
      compute_arg1(), compute_arg2(), compute_arg3(), compute_arg4());
```

### Expressions

Multi-line expressions should be indented as follows:

```cpp
const bool is_fixed_point_get = !lower_doc_key.empty() &&
                                upper_doc_key.HashedComponentsEqual(lower_doc_key);
const auto mode = is_fixed_point_get ? BloomFilterMode::USE_BLOOM_FILTER :
                  BloomFilterMode::DONT_USE_BLOOM_FILTER;
```

Or as follows:

```cpp
const bool is_fixed_point_get =
    !lower_doc_key.empty() &&
    upper_doc_key.HashedComponentsEqual(lower_doc_key);
const auto mode =
    is_fixed_point_get ? BloomFilterMode::USE_BLOOM_FILTER :
    BloomFilterMode::DONT_USE_BLOOM_FILTER;
```

The following style is also [widely used](https://gist.github.com/ttyusupov/fea3736f0265c11c4b1e8bc4d1e69f93) in our codebase, so it is acceptable to leave it as is when modifying the surrounding code,
but the two options described above are preferable for new code.

```cpp
const bool is_fixed_point_get = !lower_doc_key.empty() &&
    upper_doc_key.HashedComponentsEqual(lower_doc_key);
const auto mode = is_fixed_point_get ? BloomFilterMode::USE_BLOOM_FILTER :
    BloomFilterMode::DONT_USE_BLOOM_FILTER;
```

### Command-line flag definitions

Prefer putting the flag name on the same line with `DEFINE_...` to make the code more "greppable".
Follow function-like macro invocation styles with [all arguments on one line]({{< relref "#func-call-all-args-one-line" >}}) or with [aligned arguments]({{< relref "#func-call-aligned-args" >}}) defined above, depending on whether all three arguments of the gflag definition fit on one line.

```cpp
DEFINE_bool(create_table,
            true,
            "Whether the table should be created. Its made false when either "
            "reads_only/writes_only is true. If value is true, existing table will be deleted and "
            "recreated.");
```

Note that in the above code style we have aligned the first line of the string constant (the flag description) with its other lines. Some coding styles, including CLion's standard behavior, would indent the second and later lines by four spaces, but in our coding style we prefer to keep them aligned.

### Forward declarations

Using forward declarations is OK in a header file, if the class you are referencing is to be defined in the related .cc file, essentially making it a private class implementation. This helps keep the header file cleaner and then also keep the definition and implementation of the private class together with the actual implementation of the main class.

We also frequently use special header files named `..._fwd.h` that forward-declare various classes and declare some types, including enums, to avoid including full class declarations wherever possible and reduce compilation time.

### Parameter ordering

Normally, in function parameter lists in YugabyteDB, we put input parameter first, then output parameters.
However, we have chosen not to apply this rule to parameters that are not pointers to variables to be assigned to by the functions, but are simply non-const objects that the function calls some methods on that modify its state.
E.g. in the following function it is OK that writer is in the middle of the parameter list, because the function is not calling the assignment operator on `*writer` but simply calls some member functions of `*writer` that modify its internal state,
even though this is how this particular function produces its "output".

```
void Tablet::EmitRocksDBMetrics(std::shared_ptr<rocksdb::Statistics> rocksdb_statistics,
                                JsonWriter* writer,
                                const MetricJsonOptions& opts) {
```

### Pointer and Reference parameters amd variables

In C++ style guides frequently there is a debate about which side of `*` and `&` to put spaces on.
In our coding style, we can put a space on either side of `*` and `&` (but not on both sides), so both of the following examples are correct:

```cpp
  Status GetSockAddrorTS(const std::string& ts_uuid, Sockaddr* ts_addr);
```
and

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

### Using the Get prefix for getters

Some C++ coding styles (e.g. Google's) use the `Get` prefix for functions returning a value, and some don't (Boost, STL).
In YugabyteDB code it is allowed to either use or not use the Get prefix. There are [many](https://gist.githubusercontent.com/mbautin/97c509b3b0ec206d87cdf5a225faa515/raw) different function names in our codebase with the `Get` prefix.
The `Get` prefix would be especially useful if the function name could be misinterpreted as a verb or a verb phrase without it, but is allowed regardless. It is also allowed not to use the `Get` prefix.

One rule of thumb for deciding whether to use the `Get` prefix for functions is to look at other functions in the same class, file, or subsystem, and adhere to the prevalent naming style in the surrounding code.

### Code duplication

We should always try to reduce code duplication by extracting repeated code into reusable functions, classes, or templates. Reducing code duplication by using macros is also allowed, but please try not to overuse them and use non-macro-based ways to reuse code duplication as much as possible. Please undefine macros that are only used in an isolated section of code after they are not needed anymore.

### Switch statements over enums

If we don't use the default statement in a switch over an enum, the compiler will warn us if some values are not handled (and we make that warning an error). This allows to enforce that all enum values are being handled by a switch over an enum, if that's our intention. This complicates default case handling a bit, though. If every case is followed by a return, we can simply move the default handler to right after the end of the switch statement, e.g:

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

In this case, when someone adds a new request type, e.g. OperationType::kUpdate, we'll get a compile error asking us to add it to the switch statement.
In case there is no return following each case handler, we can still detect the default case by e.g. setting a boolean flag.

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

Note that `FATAL_INVALID_ENUM_VALUE` will terminate the process, so for functions returning a `Status` we should handle invalid enum values gracefully. We will add specific examples of that to this document.

### Arguments passed by value

Avoid using const in function (including member function) declarations when the argument is passed by value. Use const in function (including member function) definitions wherever appropriate to indicate enforce that the object is not modified inside the function body.

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
  // Implementation.
}
```

Here is an example of what not to do (in `widget.h`):

```cpp
void ProcessWidget(const WidgetType widget_type,  // BAD: "const" should be removed!
                   const int widget_cost.         // BAD: "const" should be removed!
                   const Widget& widget);         // OK: "const" is part of const reference.
```

### The `using` keyword and namespaces

We allow using the `using` directive for inner utility namespaces, for example:

```
using namespace std::placeholders;
using namespace std::literals;
using namespace yb::size_literals;
```

This allows us to use various convenient literals, e.g. `100ms` to denote "100 milliseconds".

### Unnamed namespaces vs. static keyword

When defining functions that should only be accessible within a `.cc` file, prefer using unnamed namespaces to using the static keyword.
See e.g. [this](https://stackoverflow.com/questions/154469/unnamed-anonymous-namespaces-vs-static-functions) discussion.

### Static and global variables

Using static objects is _allowed_ in our coding style with a few limitations and understanding the static objects life-cycle. The limitations:
* The static global variables **must** be totally independent. The order of constructions/destructions should not matter.
* The objects **must not** lock anything or allocate global resources (except memory).
* The objects should be as simple as possible.

If you are adding new static objects, you need to remember:
* Global static objects (as well as static class fields) are constructed before the `main()` function call.
* Local static objects are constructed on the first coming to the variable scope (that’s why this option is better than global static).
* There is a [locking mechanism](http://stackoverflow.com/questions/8102125/is-local-static-variable-initialization-thread-safe-in-c11) needed so that a concurrent execution waits for initialization completion if the static variable is already being initialized by an earlier thread. E.g. for a simple static variable defined in a function

```cpp
void f() {
    static string s = "asdf";
}
```

the generted code might be using a lock automatically to guard the static variable initialization.

```
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

Static objects are destructed after the end of the `main()` function.
The order of global static objects construction is undefined, and it can even change from build to build.
The order of static objects destructions is reverse of the order of constructions, so it is also undefined.

### Multiple inheritance

Multiple inheritance is ALLOWED in general with a few limitations regarding its use:
* Forbidden pointers/references down-casting (casting from base to derived class).
  (Note: the only way to do it safely in C++ is using the `dynamic_cast<...>` operator, which relies on run-time type information (RTTI), and we may decide to disable RTTI in release builds at some point for performance reasons.
* Use protected/private inheritance if possible.
* Do not use multiple inheritance if a base class is part of this object from architectural point of view. Prefer composition (making the "part" object a member field in the "whole" object) in such cases.

### Testing whether a pointer is null

It is OK to use implicit bool conversion for raw / unique / shared pointers:

```cpp
if (p) {
  p->DoSomething();
} else {
  ...  // Handle the nullptr case
}
```

Another example:

```
LOG(INFO) << (p ? p->DebugString() : "N/A");
```

### Commenting code

Obvious comments like the following are not adding anything useful and should be avoided:

```
  // Returns transaction ID.
  const TransactionId& id() const;
```

Functions and classes in header files should be reasonably well commented, but the comments should not duplicate what is already obvious from the code. In fact, if the code can be restructured, or if functions or classes could be renamed to reduce the need for comments, that is the preferred way.

Functions and classes in `.cc` files don't have to be commented as extensively as code in header files. However, please add comments and examples for anything that you consider non-obvious to a potential new reader of your code.

### CHECKs vs DCHECKs vs returning a Status

We sometimes use `DCHECKs` to verify function prerequisites.
* If we never expect an incorrect parameter value to be passed into the function, because there is validation happening in the calling function, it is OK to keep that as a `DCHECK`.
* If we could theoretically get bad data in production, then:
  * If we can recover from this error, we should return an error `Status`.
  * If this is a severe invariant violation and we can't recover from it, this could be a `CHECK`.

Note that for the returning a `Status` in case of errors, we also have the `SCHECK` macro, which will `CHECK` in debug builds, but return an error `Status` in release builds.

### `PREDICT_TRUE` and `PREDICT_FALSE`

Do not use these macros unless proven that they improve performance.

### `Result` vs. `Status` with output parameters

Much of our code is wired to return `Status`, so we are able to get a sense of if the function completed successfully (OK), or if there was some kind of an error.
However, we sometimes want to also get legitimate output from these functions. We used to do this by using function signatures such as

```cpp
Status foo(int* return_variable);
```

However, now we have a better way to achieve the same goal, the `Result` type, which can encapsulate both an output parameter (in the successful case) and a `Status` (in case of an error):

```cpp
Result<int> foo();
```

For new code it is advisable to use `Result`.

### String formatting

Prefer the `Format` function to `Substitute` to produce formatted strings. They have somewhat
similar syntax, with inline substitution parameters `$0`, `$1`, etc., but the `Format` function has
several advantages:

* It uses the `ToString` utility, so it could convert many different types of objects to strings, e.g. collections, protobufs, or any class with a `ToString` member function.
* You don't have to call `arg.ToString()`, just put `arg` to the `Format` function as is and it will call `ToString` on it for you.
* `Format` is a bit faster than `Substitute` on some benchmarks.

### `consensus::OpId`

`consensus::OpId` is just an alias for `yb::OpIdPB`, a protobuf class.
It should be used only where we really need to use protobuf, for example inside other protobuf messages. For the rest of the code `yb::OpId`, a normal C++ class, should be used.

### Thread safety analysis

We use Clang's [Thread Safety Analysis](https://clang.llvm.org/docs/ThreadSafetyAnalysis.html) annotations in parts of our code.
Thread safety annotations are a C++ extension that provides compile-time checks for potential race conditions in code.
Annotations are extremely useful for code maintainability, because it makes locking semantics explicit and the compiler warns about accessing memory without holding the necessary mutexes.

A few more things to keep in mind:
- `std::unique_lock` does not work out of the box and we wrap it into our custom wrapper `yb::UniqueLock`.
- Similarly, for `std::shared_lock` we have `yb::SharedLock`.
- Occasionally, we still annotate some functions where thread safety analysis cannot be properly applied, with `NO_THREAD_SAFETY_ANALYSIS`, so that we can still use thread safety analysis in the surrounding code.

Our build scripts enable thread safety analysis for Clang version 11 and above, because earlier versions do not support certain features that we need. Thread safety analysis works very well on macOS with modern Clang compilers, providing instant hints if the environment is set up properly.

### Other related coding guidelines for C++

In addition to the above rules, YugabyteDB C++ coding style is based on [Google's C++ style guide](https://google.github.io/styleguide/cppguide.html).

## Protocol Buffers guidelines

Both protobuf structure and enums should have PB suffix due to following reasons:

* Better "greppability" - less false positives when searching for TableTypePB when searching for TableType
* We can have structs/classes with the same name, but without PB suffix for usage in our code.
* We can have enum wrappers similar to YB_DEFINE_ENUM with the same name as protobuf enum, but without PB suffix.
