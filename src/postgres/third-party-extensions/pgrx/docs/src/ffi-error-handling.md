Postgres is written in C.  pgrx is written in Rust.  Between them is a boundary where each blindly believes the other
behaves in an expected way.  Our primary concern with this boundary is error handling.

There are many other concerns across this boundary such as function call ABIs and pointer ownership, but these are generally
"obvious" concerns to anyone that's done any FFI development and won't be discussed here in detail.  


# High-level Postgres Error Handling Overview

Most Postgres internal functions (those accessible via the `pg_sys` module) are capable of raising an `ERROR`.  This "error"
comes into existence when, internally, Postgres code calls the `ereport()` (or `elog()`) macro.  `ereport()` does the work
to instantiate the error by first calling the `errstart()` function.

Code execution then finds its way to `errfinish()` where, finally, `siglongjmp()` is called to instantly move the stack 
back to the frame where Postgres began the current transaction (where it previously created a `sigsetjmp()` point).  

From here the code detects that it's a second return from `sigsetjmp()` and performs the necessary actions to ROLLBACK 
the current transaction. Finally, Postgres is again ready and waiting to begin a new transaction.

This is an elegant solution to error handling as it allows Postgres to cleanly rollback the current transaction, free
used memory, release locks, and whatever else might be necessary.


# High-level Rust Error Handling Overview

Rust, on the other hand, will call its panic hook and then run its panic handler when `panic!()` is called.
The panic handler itself will either *unwind* the stack or *abort* the current process.
This is, quite clearly, incompatible with Postgres' error handling approach:
unwinding destroys `sigsetjmp` checkpoints, and aborting shuts down the entire database!

It's technically incompatible, it's spiritually incompatible, and it robs Postgres of the opportunity to cleanly rollback 
the current transaction (Postgres is supposed to be tolerant of such situations, but who wants to test Postgres' 
recoverability in production?).

Conversely, Postgres' `sigsetjmp`/`siglongjmp` approach is as egregiously incompatible with Rust.  `siglongjmp` will blindly
jump over Rust stack frames, leaking Rust-allocated memory, ignoring `trait Drop` implementations, and denying Rust code 
any opportunity to participate in error handling.


# A Wolf, a goat, and some cabbage

pgrx uses two different approaches to protect these FFI boundaries.  While both are implemented in Rust, one protects
Rust from Postgres `setlongjmp` ERRORs and the other protects Postgres from Rust `panic()!`s.  To make things confusing
they're both called `#[pg_guard]`.

Essentially, pgrx needs to guard two styles of `extern "C"` functions.  One style is the [`extern "C" {}` block][extern-blocks] that 
declares a function lives "somewhere else" (in our case, the Postgres process in which the pgrx extension is loaded).
The other style is [`extern "C" fn foo() { ... }` functions][extern-fn] that are written in Rust and might be passed to Postgres (for
it to later call) via a standard function pointer.

## Guarding Postgres Internal Functions

pgrx uses the [`bindgen`] tool to generate "bindings" for exported Postgres symbols.  Postgres' source header files (`*.h`)
are read, parsed, and transformed, as much as bindgen knows how, into Rust declarations.  In the case of exported functions,
bindgen generates blocks similar to:

```rust
extern "C" {
    pub fn palloc(size: Size) -> *mut ::std::os::raw::c_void;
    // ... many more internal Postgres function definitions here ...
}
```

Then, pgrx' `build.rs` process rewrites these functions into something similar to this:

```rust
#[pg_guard]
extern "C" {
    pub fn palloc(size: Size) -> *mut ::std::os::raw::c_void;
    // ... many more internal Postgres function definitions here ...
}
```

This form of the `#[pg_guard]` macro then walks the `extern "C" {}` block items and writes new function declarations for 
each. This expansion looks similar to:

```rust
pub extern "C" fn palloc(size: Size) -> *mut ::std::os::raw::c_void {
    extern "C" {
        pub fn palloc(size: Size) -> *mut ::std::os::raw::c_void;
        // ... many other function definitions here ...
    }
    
    unsafe {
        crate::ffi::pg_guard_ffi_boundary(|| palloc(size))
    }
}
```

Essentially, in this usage, `#[pg_guard]` generates standalone wrapper functions that delegate to pgrx' `pg_guard_ffi_boundary(|| ...)`
function.  This function sets up pgrx' own `sigsetjmp` point, lies to Postgres' exception handling stack about where it's 
going to jump to in case of an ERROR, calls the function via the closure argument, then restores Postgres' exception handling
stack.

"Lies to Postgres" is a little curious here, but Postgres doesn't expect that whenever it raises an ERROR it'll be jumping
into a Rust-created stack frame.  In fact, it doesn't have *any* expectations about where it's jumping other than, eventually,
the raised ERROR will either rollback the current transaction, be re-thrown, or in some cases, simply ignored.

As it relates to this document, the specific workings of this process is more of an implementation detail, but the gist of
the process is that we set up our own `sigsetjmp` point so that we can trap, in Rust, any ERROR Postgres might raise while
calling the internal Postgres function.  This then allows us to convert that error into a Rust panic and have it propagated
through the call stack so that Rust's stack properly unwinds and type destructors are called.

We make sure to limit the amount of `sigsetjmp`-protected code to only be the internal Postgres function being called.
Doing so ensures we're doing the minimal amount of work necessary to properly protect from a Postgres ERROR and not also
accidentally defeating Rust's stack unwinding.

While Rust doesn't guarantee that `drop()` will get called for any instantiated type, we do our best to encourage it.
Of course, using `std::mem::forget()` on an instantiated type will never have its drop implementation called.

Ultimately, at the top of the Rust callstack, the panic raised from a Postgres ERROR is then converted back into a normal
Postgres ERROR using its internal facility for raising errors.

Generally speaking, pgrx extension developers don't need to worry about this, as this is all machine-generated at compile-time.
They can, however, manually create `extern "C" {}` blocks with a `#[pg_guard]` annotation if they wish to write their own
wrappers for specific internal Postgres functions that aren't yet exposed by pgrx.  The project, of course, would prefer
pull requests to expose such functions through header inclusion.

## Guarding User Functions

The other way `#[pg_guard]` is used is for Rust functions that are `extern "C"`.  These would be functions in the Rust
shared library that Postgres calls.  The intent here is that such functions guard against Rust panics, so that they may
be properly converted into Postgres ERRORs.  It's the opposite direction of the above.

Examples of these types of functions are any that are annotated with `#[pg_extern]`, in which the macro properly expands
to the necessary code, and other functions where it's necessary to give Postgres a pointer to that function -- the various
planner/executor hooks is an example of this.

In this case, `#[pg_guard]` is used as follows:

```rust
#[pg_guard]
extern "C" fn foo() -> bool {
    // ... user-written Rust code here ...
    return true;
}
```

During compilation, the macro will expand to something similar to:

```rust
extern "C" fn foo() -> bool {
    pgrx::pg_sys::submodules::panic::pgrx_extern_c_guard(move || {
        // ... user-written Rust code here ...
        return true;
    })
}
```

Behind the scenes, `pgrx_extern_c_guard(|| ...)` executes the closure argument inside a rust `std::panic::catch_unwind(|| ...)`
block.  Doing so allows pgrx to capture any Rust `panic!()` and contain its stack unwinding to within the [`catch_unwind`]
which allows for Rust destructors to be run, Rust to free memory, and for pgrx to ensure we don't end up aborting the
backend process.

When control is returned to `pgrx_extern_c_guard()`, the captured panic is converted into a Postgres ERROR and raised.
Ultimately, this will ROLLBACK the current database transaction.  It will not abort the backend process.

Catching Rust panics and converting to Postgres ERRORs ensures that user code (in Rust) doesn't try to unwind the stack
back into Postgres' stack, which is managed by the C runtime.  Failure to use `#[pg_guard]` on a Rust `extern "C" fn` 
that `panic!()`s will absolutely cause a segfault.


## Getting Across the Bridge

The most common scenario where all this is wired together is in exposing Rust functions as SQL functions with `#[pg_extern]`.
Imagine you've created a function called `strlen()` that, given a `String` returns its length...

```rust
fn strlen(input: String) -> i64 {
    input.len() as i64  // postgres doesn't support unsigned ints -- irrelevant implementation detail
}
```

... and you want this to be exposed as a SQL function.  To do so you simply add the `#[pg_extern]` annotation:

```rust
#[pg_extern]
fn strlen(input: String) -> i64 {
    input.len() as i64  // postgres doesn't support unsigned ints -- irrelevant implementation detail
}
```

Now, you've got a function you can use via sql:

```sql
[postgres] # SELECT strlen('hello, world');
```

At compile time, pgrx has rewritten this `strlen` function to look more like the below.  It's not *exactly* this, but the
exact code is an implementation detail subject to change...

```rust
extern "C" fn strlen(input: String) -> i64 {
    pgrx_extern_c_guard(|| input.len() as i64)
}
```

Lets say you have another function that, for some unknown reason, wants to open and then close a relation (table):

```rust
use std::time::Duration;

#[pg_extern]
fn rel_open_close(oid: pg_sys::Oid) {
    struct Foo;
    impl Drop for Foo {
        fn drop(&mut self) {
            eprintln!("Foo got dropped");
        }
    }

    unsafe {
        let _foo = Foo;
        let rel = pg_sys::relation_open(oid, pg_sys::AccessShareLock);
        
        std::thread::sleep(Duration::from_secs(10));    // this is just an example
        
        pg_sys::relation_close(rel, pg_sys::AccessShareLock);
        
        // `_foo` should drop() here
    }
}
```

You can imagine that the above gets "expanded", at compile time, into something similar to:

```rust
use std::time::Duration;

extern "C" fn rel_open_close(oid: pg_sys::Oid) {
    pgrx_extern_c_guard(|| {
        struct Foo;
        impl Drop for Foo {
            fn drop(&mut self) {
                eprintln!("Foo got dropped");
            }
        }

        unsafe {
            extern "C" {
                fn relation_open(oid: pg_sys::Oid, lmode: pg_sys::LOCKMODE) -> *mut pg_sys::RelationData;
            }
            let rel = pg_guard_ffi_boundary(|| relation_open(oid, pg_sys::AccessShareLock));
            
            std::thread::sleep(Duration::from_secs(10));    // this is just an example

            extern "C" {
                fn relation_close(rel: pg_sys::RelationData, lmode: pg_sys::LOCKMODE);
            }
            pg_guard_ffi_boundary(|| relation_close(rel, pg_sys::AccessShareLock));

            // `_foo` should drop() here
        }
    })
}
```

Combined, we're ensuring that if any of the Postgres functions (`relation_open`/`relation_close`) raise an ERROR (say, due
to an invalid Oid value), `pg_guard_ffi_boundary` will catch that and convert into a Rust panic.  Then ultimately, the top-level
`pgrx_extern_c_guard` call will convert it back into a Postgres ERROR once the Rust stack has properly unwound and drop
impls have been called.

[`mem::forget`]: https://doc.rust-lang.org/std/mem/fn.forget.html
[`catch_unwind`]: https://doc.rust-lang.org/std/panic/fn.catch_unwind.html
[`bindgen`]: https://rust-lang.github.io/rust-bindgen/
[extern-blocks]: https://doc.rust-lang.org/reference/items/external-blocks.html
[extern-fn]: https://doc.rust-lang.org/reference/items/functions.html#extern-function-qualifier