This document attempts to describe the general design decisions `pgrx` has made as it relates to providing a Rust-y API
around Postgres internals.

## A Note from @eeeebbbbrrrr

pgrx was developed out of frustrations with developing Postgres extensions in C and SQL, specifically @ZomboDB. ZomboDB 
quickly evolved into a fairly large extension.  Roughly 25k lines of C, 8k lines of hand-written SQL, and all sorts of 
bash machinery to accommodate testing, releases, and general project maintenance.  In C, it is just too much for one 
person to manage.  I needed a hand and the Rust compiler seemed like a good coworker.

Today, ZomboDB is still about 25k lines -- but of Rust!, about 1k lines of hand-written SQL, and much less bash
machinery.  And the Rust compiler really is a good coworker.

## Prior Art

- https://github.com/jeff-davis/postgres-extension.rs
- https://github.com/bluejekyll/pg-extend-rs
- https://github.com/wasmerio/wasmer-postgres

None of these appear active today.

## Goals

pgrx' primary goal is to make Rust extension development as natural to Rust programmers as possible.  This generally 
means doing the best we can to avoid violating [the principle of least astonishment](https://en.wikipedia.org/wiki/Principle_of_least_astonishment) 
as it relates to error handling, memory management, and type conversion.

As part of this goal, pgrx tries to provide larger Rust APIs around internal Postgres APIs.  One such example is pgrx' 
safe Rust API for creating Postgres aggregate functions.  The developer implements a single trait, 
`pgrx::aggregate::Aggregate`, described in safe Rust, and pgrx handles all the code and SQL generation to expose the 
aggregate function to the SQL-level.

pgrx also provides "unsafe" access to all of Postgres' internals.  This allows developers (and pgrx itself) to directly
use internals not yet wrapped with safe Rust APIs.

The secondary goal, which is not any less important, is an SQL schema generator that programmatically generates 
extension SQL from the extension Rust sources.  Other than solving the lazy programmer problem, the schema generator 
ensures certain SQL<-->Postgres<-->Rust invariants are maintained, which includes typing for function arguments and 
return values.

Finally, pgrx wants to improve the day-to-day extension development experience.  `cargo-pgrx` is the Cargo plugin that 
does the heavy lifting of manging Postgres instances for running and testing an extension.

## Non-Goals

pgrx does not aim to present safe wrappers around Postgres internals in a one-to-one mapping.  Where it can (and so far 
has), it endeavors to wrap larger APIs that look and feel like Rust, not Postgres' version of C.  This is an 
enormous undertaking which will continue for many years.

## Rust Bindings Generation

pgrx uses [`bindgen`](https://github.com/rust-lang/rust-bindgen) to generate Rust "bindings" from Postgres' C headers.
Ultimately, Rust versions of Postgres' internal structs, typedefs, functions definitions, globals, and enums are 
generated.

The generated bindings are part of the sibling `pgrx-pg-sys` crate and are exported through `pgrx` as the `::pg_sys` 
module.  One set of bindings are generated for each supported Postgres version, and the proper one is used based on Rust 
conditional compilation feature flags.

A bit of post-processing is done to provide some developer conveniences.

First, a graph is built of all Postgres internal structs that appear to be a Postgres `Node`.  The resulting struct 
definitions are then augmented to `impl Display` (via `pg_sys::nodeToString()`) and to also`impl PgNode`, which is 
essentially a marker trait.

Additionally, pgrx auto-generates a Rust `PgBuiltInOids` enum from the set of `#define xxxOID` defines found in the
Postgres headers.

pgrx reserves the right to generate other convenience types, traits, etc in the future.  Such things may come from user
requests or from the need to ensure more robust compile-time type checking and safety guarantees.

The bindings are generated via the `build.rs` script in the `pgrx-pg-sys` crate.  As some of the Postgres header files 
are themselves machine-generated, pgrx requires its bindings be generated from these headers.  When pgrx is managing 
Postgres instances itself (via `cargo-pgrx`), it gets them from there, otherwise it can use locally installed distro
packages that include the header files.

## Error Handling

Postgres has a few different methods of raising errors -- the `ERROR`, `FATAL`, and `PANIC` levels.  `ERROR` aborts
the current transaction, `FATAL` terminates the raising backend process, and `PANIC` restarts the entire cluster.

Rust only has one: `panic!()`, and it either terminates the process, or unwinds until the nearest catch_unwind,
terminating the thread if it reaches the top, and the process if done from the thread that runs main().

pgrx, wanting to be as least surprising as possible to Rust developers, provides mechanisms for seamlessly handling
Postgres `ERROR`s and Rust `panic!()`s.  There are two concerns here.  One is that Rust developers expect proper drop 
semantics during stack unwinding.  The other is that Postgres expects the *transaction* to abort in the face of a 
recoverable error, not the entire process.

### Protecting the Rust Stack (read: lying to Postgres)

Postgres uses the POSIX `sigsetjmp` and `longjmp` functions to implement its transaction error handling machinery.
Essentially, at the point in the code where a new transaction begins, Postgres creates `sigsetjmp` point, and if an 
ERROR is raised during the transaction, Postgres `longjmp`s back to that point, taking the other branch to abort the 
transaction and perform necessary cleanup.  This means that Postgres is jumping to a stack frame from an earlier point 
in time.

Jumping across Rust stack frames is unsound at best, and completely unsafe at worst.  Specifically, doing so defeats any
Rust destructor that was planned to execute during normal stack unwinding.  The ramifications of this can vary from 
leaking memory to leaving things like reference-counted objects or locks completely unaware that they've been released.
If Rust's stack does not properly unwind, it is impossible to know the program state after `sigsetjmp`'s second return.

With this in mind, it's also important to know that any Postgres internal function is subject to raise an ERROR, causing
a `longjmp`.

To solve this, pgrx creates its own `sigsetjmp` points at each Postgres FFI boundary. As part of pgrx' machine-generated 
Rust bindings, it wraps each Postgres function, adding its own `sigsetjmp` handling prior to calling the internal 
Postgres function.  The wrapper lies and tells Postgres that if it raises an error it should `longjmp` to this 
`sigsetjmp` point instead of the one Postgres created at transaction start.  Regardless of if an ERROR is raised, pgrx 
restores Postgres' understanding of the original `sigsetjmp` point after the internal function returns.  These wrapper 
functions are what's exposed to the user, ensuring it's not possible to bypass this protection.

(as an aside, I did some quick LLVM IR callgraph analysis a few years ago and found that, indeed, nearly every Postgres
function can raise an ERROR (either directly or indirectly through its callgraph).  I don't recall the specific numbers,
but it was such a small percentage that didn't call `ereport()` that I didn't even consider adding such smarts to pgrx' 
bindings generator)

When Postgres `longjmp`s due to an ERROR during a function call made through pgrx, its wrapper function's `sigsetjmp` 
point activates and the ERROR is converted into a Rust `panic!()`.  This panic is then raised using Rust's normal panic 
machinery.  As such, the Rust stack unwinds properly, ensuring all Rust destructors are called.

After Rust's stack has unwound, the `panic!()` is handed off to `ereport()`, passing final error handling control back 
to Postgres.

This process is similar to Postgres' `PG_TRY`/`PG_CATCH` C macros.  However, it happens at *every* Rust-->Postgres 
function call boundary, allowing Rust to unwind its stack and call destructors before relinquishing control to 
Postgres.

### Protecting Postgres Transactions (read: asking Rust for help)

The above is about how to handle Postgres ERRORs when an internal Postgres function is called from Rust.  This section
talks about how to handle Rust `panic!()`s generated by a Rust function called from Postgres.

A decision was made during initial pgrx development that **all** Rust `panic!()`s will properly abort the active
Postgres transaction.  While Rust's preference is to terminate the **process**, this makes no sense in the context of a
transaction-aware database and would be quite astonishing to even the most general Postgres user.

Postgres has many places where it wants an (in Rust-speak) `extern "C"` function pointer.  Probably the most common 
place is when it calls an extension-provided function via SQL.

When the function is written in Rust, it is important to ensure that should it raise a Rust `panic!()`, the panic is 
properly converted into a regular Postgres ERROR.  Trying to propagate a Rust panic through Postgres' C stack is 
undefined behavior and even it weren't, Rust's unwind machinery  would not know how to instead `longjmp` to Postgres' 
transaction `sigsetjmp` point.

pgrx fixes this via its `#[pg_guard]` procmacro.  pgrx' `#[pg_extern]`, `#[pg_operator]`, etc macros apply `#[pg_guard]` 
automatically, so in general users don't need to think about this.  It does come up when a pgrx extension needs to 
provide Postgres with a function pointer.  For example, the "index access method API" requires a number of function 
pointers. In this case, each function needs a `#[pg_guard]` attached to it.

`#[pg_guard]` sets up a "panic boundary" using Rust's `std::panic::catch_unwind(|| ...)` facility.  This function takes 
a closure as its argument and returns a `Result`, where the `Error` variant contains the panic data.  The `Ok` variant, 
of course, contains the closure's return value if no panic was raised.

Everything executed within the `catch_unwind()` boundary adheres to proper Rust stack unwinding and destructor rules.
When it does catch a `panic!()` pgrx will then call Postgres' `ereport()` function with it.  How its handled from there
depends how deep in the stack we are between Rust and internal Postgres functions, but ultimately, at the end of the
original `#[pg_guard]`, the panic information is properly handed off to Postgres as an ERROR.

---

Together, these two error handling mechanisms allow a pgrx extension to safely raise (and survive!) runtime "errors" from 
both Rust and Postgres and "bounce" back-n-forth through either type.  Assuming `#[pg_guard]` is properly applied a call 
chain like `Postgres-->Rust-->Rust-->Rust-->Postgres-->Rust(panic!)` will properly unwind the Rust stack, call Rust 
destructors, and safely abort the active Postgres transaction. So will a call chain like 
`Postgres-->Rust-->Postgres-->Rust-->Rust-->Postgres(ERROR)`.

## Memory Allocation

Postgres uses a system it calls `MemoryContext`s to manage memory.  A `MemoryContext` lives for a certain amount of time
(not to be confused with Rust's concept of lifetimes), and typically this is no longer than the life of the current
transaction.  When Postgres decides a `MemoryContext` is no longer needed, it is freed en masse. Combined with Postgres 
transaction and error handling (described above), memory contexts serve as a simple garbage collection system, ensuring 
a transaction doesn't leak memory.

The most common context is the `CurrentMemoryContext`.  Executing code is run with `CurrenteMemoryContext` set to a 
valid context and generally doesn't need to know how long it lives.  Except more often then not, executing code needs to 
decide how long some allocated bytes live.  It's quite common for Postgres internals, and extensions, to "switch to" a
differently-lived `MemoryContext`, allocate some bytes, and switch back to the previous `CurrentMemoryContext`.
Failure to do so can result in, at least, use-after-free bugs.

Rust, on the other hand, decides **at compile time**, when allocated memory should be freed.  The Rust compiler also 
guarantees that all allocated objects are valid when they're used (barring use of `unsafe`) and aren't mutated 
concurrently.  These approaches aren't exactly compatible.

### The Rust Compiler is Smarter than You Are

Working under the premise that pgrx wants to be unsurprising to Rust developers, the only clear option is to let Rust and 
the compiler do what they do, independent of Postgres' memory context system.  Rust developers *want* the compiler to 
complain when they attempt to use memory before it's been allocated, after it's been freed, and try to concurrently
mutate it.

pgrx does not replace Rust's `GlobalAllocator`.  If it did, it would presumably use the Postgres memory context system, 
allocating against whatever `CurrentMemoryContext` happens to be.  The idea of a compiler reasoning about the lifetime 
of allocated bytes coming from a runtime allocation system that doesn't follow the same rules is not very persuasive.
Additionally, Postgres is **not** thread-safe, which includes its memory management system.  

It is a feature, however, that pgrx extensions can take advantage of Rust's excellent concurrency support, so long as 
they follow certain conditions (more on thead safety below).

The goal is that developers shouldn't need to worry about `CurrentMemoryContext` because they're not using it.  Just as 
in C, however, there are cases where allocated bytes need to live longer than the Rust compiler will allow.
Typically, this is dictated by a particular internal Postgres API.

For example, the set returning function API (SRF) requires that the "multi-call state" be allocated in a specific 
`MemoryContext`. To help with this, pgrx provides direct unsafe access to Postgres' memory context apis through the 
`pg_sys` module.  It also provides an enum-based wrapper, `PgMemoryContexts`, which exposes a safe API for working with
memory contexts, including switching Postgres to a different one.

### Who am I, why am I here?

Having two different approaches to memory allocation leads to bytes coming from one or the other.  As it relates to 
bytes/pointers provided by pgrx, the rules are fairly straightforward:

If pgrx gives or wants a...

  1. pointer (`mut *T`), it is allocated/freed by Postgres
  2. `PgBox<T>`, its pointer is allocated by Postgres but may be freed by Rust during `drop()`, depending on construction
  3. Rust reference (`&T:IntoDatum` or `&mut T:IntoDatum`), it is allocated/freed by Postgres
  4. owned `T`, it is freed by Rust but may be holding Postgres-allocated pointers which are freed during `drop()`

`PgBox<T, WhoAllocated>` is a type for managing a `palloc()`'d pointer.  That pointer could have come to life as the 
return value of an internal Postgres function, allocated directly via `palloc()`, or by `PgBox` itself.

Encoded in `PgBox`'s type is who performed the allocation -- Postgres or Rust.  When it's thought to be 
`AllocatedByPostgres`, then it is left to Postgres to free the backing pointer whenever it thinks it should (typically 
when its owning `MemoryContext` is deleted).  Only if it's thought to be `AllocatedByRust` is the backing pointer is 
freed (via `pfree()`) when the `PgBox` instance is dropped.

The `WhoAllocated` trait is a Zero Sized Type (ZST) and incurs no runtime overhead in terms of memory or branching -- 
the compiler takes care of the implementation details through monomorphization.  Additionally, `PgBox` itself is only
the size of a pointer.

Functions such as `::alloc<T>()` exist (which delegate to `palloc()`) to create a new `PgBox` wrapping a pointer to a
specific type.

The `::into_pg(self)` function will relinquish `PgBox`'s ownership of the backing pointer and simply return it, which in
turn passes responsibility of freeing the pointer to Postgres when it sees fit.

`PgBox` also provides `Deref` and `DerefMut` implementations which will, as a safety measure, raise a Postgres ERROR 
if the backing pointer is NULL.  These exist as a convenience for slightly more fluent code.

(as an aside, the `WhoAllocated` trait which determines much of `PgBox`'s behavior is poorly named.  It should be
thought of as "WhoDrops", as it doesn't matter if the user calls `palloc()` directly and provides the pointer, if
`PgBox` calls `palloc()` itself, or if it consumes a pointer provided by Postgres.  The decision it is making is if the 
backing pointer should be freed following Rust's normal drop semantics or not)

### The Path to a Longer Life

Common concerns around "who allocated what and how long does it live" are typically centered around Datums (see
below), passing pointers to internal Postgres functions, and holding on to pointers (regardless of source) for
longer than the current Rust scope or life of `CurrentMemoryContext`.

For the later case where Rust-allocated bytes need to live longer than the compiler will allow, `PgMemoryContexts.leak_and_drop_on_delete(T)`
allows the thing to be leaked (returned to the caller as `*mut T`) and later dropped when Postgres decides to delete 
(free) the `MemoryContext` to which it was leaked.  The returned pointer can be stashed somewhere and later 
re-instantiated via `PgBox::from_pg()`.

## Thread Safety

As it relates to any Postgres thing (calling a function, allocated memory, anything at all from `pgrx::*::*`), **there is 
none!**  Postgres itself is purposely not thread safe and neither is pgrx.

It's difficult (right now) to ask the Rust compiler to ensure code is not using pgrx from a thread.  We have been 
discussing some ideas.  One method of enforcing thread safety is via using `!Send` types that can only be constructed
from a single thread.  However, this can be unnecessarily punishing, as this approach must enforce that a type is
only constructed on one thread, which can interfere even with constructing and using it from a single thread.  As-is,
currently, `pgrx` allows things like `direct_function_call`, so that calling a function in Postgres is simply, well,
calling a function.  Making that impossible is probably an unviable ergonomics regression for the simpler case
of a Postgres extension that doesn't attempt to do multithreading.

Until then, there's one simple rule:

- Do **not** use anything from the `pgrx` crate, or any Postgres allocated memory, from a thread

You might be really smart and think you can sneak by doing something, but you're probably wrong.  Even if you're right,
there's no help from the Rust compiler here, which means you're wrong.

pgrx does attempt to detect if a Postgres `extern "C"` function is called from a thread that isn't the main thread
and it will `panic!()` if not, but this is only a runtime check. It is not something that is inherently foolproof.

That said, it is okay for a pgrx extension to use threads as long as they follow that one simple rule.  For example,
there's no reason why an extension can't import `rayon` and parallel sort a Vec or take advantage of its amazing
parallel iterators.  It's even fine to spawn a thread during `_PG_init()` and have it out there in the background
doing whatever it does -- so long as it's not, not even a tiny bit, trying to use anything provided by pgrx.

Rust `panic!()`s from a thread do not interface with the pgrx error handling machinery.  Whatever Rust normally does in 
that situation is what happens with pgrx -- the thread will die.

## Type Conversions

pgrx provides two traits for working with Postgres `Datum`s:  `FromDatum` and `IntoDatum`.  The former, given a `Datum`
and its "is null" flag, creates a Rust type in the form of `Option<T>`.  The latter, given a Rust type, creates the
equivalent Postgres `Datum`.

Postgres datums are either pass-by-value or pass-by-reference.  Pass-by-value types, such as a `int4` have direct 
conversions to the corresponding Rust type (`i32` in this case).  

Pass-by-reference types are detoasted and either copied into a Rust-owned type or represented as a zero-copy reference.
For example, pgrx can represent the Postgres `text` datum as either a Rust-owned `String` or a `&str`.  Similar treatment
exists for other pass-by-reference types such as `bytea` (`Vec<u8>` or `&[u8]`).

In the case of converting a datum to a Rust reference, the backing memory is allocated in `CurrentMemoryContext`.
However, pgrx knows that in some situations, it needs to be allocated elsewhere and handles this automatically.  For
example, pgrx' SRF API implementation knows to allocate function arguments of type `&T:FromDatum` in the "multi-call 
context".  Similarly, pgrx' SPI API implementation knows to allocate datums returned from statements in the parent memory 
context so that the value can outlive that specific pgrx SPI session.

When a Rust type is converted to a Datum, if it's pass-by-value, then it's a straightforward primitive cast.  When it's
pass-by-reference, the proper Postgres pointer type is allocated in the `CurrentMemoryContext` and initialized in the
way it most makes sense for that type.

pgrx represents its full understanding of a decoded datum as `Option<T>`.  This enables working with NULL values.

Notes about some special-case handling:

### TEXT/VARLENA

Rust **requires** Strings to be UTF8.  If they're not, it's undefined behavior.  pgrx assumes that `text` it receives
from Postgres **is** UTF8.  This means pgrx extensions, mainly those that do anything with text, won't necessarily work
well in non-UTF8 Postgres databases.

### Time Types

pgrx is zero-copy for Postgres' time types and can be converted to/from `u64` and `time::PrimitiveDateTime`.  Future work
will involve implementing various Rust traits to provide operations such as addition and subtraction, including with
intervals.  These will delegate to Postgres where appropriate.

### ARRAY

pgrx Array handing is, as of this writing, receiving an overhaul to correct some memory safety issues and provide, when
it can, a zero-copy view over the backing Postgres `ArrayType` instance.  Likely, pgrx' ArrayType implementation will
end up being something akin to an opaque `impl std::iter::Iterator<Item = Option<T>>`.

### NUMERIC

Numeric is being overhauled to be a zero-copy Rust type that will delegate all its math operations directly to the 
corresponding Postgres functions.  For example, `impl Add<Numeric> for Numeric` will delegate to `pg_sys::numeric_add()`.

### JSONB

Currently, JSONB support incurs the overhead of serializing to a string, and then being deserialized by `serde`. 
Perhaps a `serde::Deserialize` implementation that understands the JSONB format and allows borrows is possible.

## `cargo-pgrx` is Your Friend

Please see the [cargo-pgrx documentation](https://github.com/pgcentralfoundation/pgrx/blob/master/cargo-pgrx/README.md)

## `postgrestd` Interactions

TODO:  @workingjubilee, could I get a little help?
