//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! `pgrx` is a framework for creating Postgres extensions in 100% Rust
//!
//! ## Example
//!
//! ```rust
//! use pgrx::prelude::*;
//!
//! // Convert the input string to lowercase and return
//! #[pg_extern]
//! fn my_to_lowercase(input: &str) -> String {
//!     input.to_lowercase()
//! }
//!
//! ```
#![cfg_attr(feature = "nightly", feature(allocator_api))]

#[macro_use]
extern crate bitflags;
extern crate alloc;

use once_cell::sync::Lazy;
// expose our various derive macros
pub use pgrx_macros;
pub use pgrx_macros::*;

/// The PGRX prelude includes necessary imports to make extensions work.
pub mod prelude;

pub mod aggregate;
pub mod array;
pub mod atomics;
pub mod bgworkers;
pub mod callbacks;
pub mod callconv;
pub mod datum;
pub mod enum_helper;
pub mod fcinfo;
pub mod ffi;
pub mod fn_call;
pub mod guc;
pub mod heap_tuple;
#[cfg(feature = "cshim")]
#[allow(deprecated)]
pub mod hooks;
pub mod htup;
pub mod inoutfuncs;
pub mod itemptr;
pub mod iter;
pub mod layout;
pub mod list;
pub mod lwlock;
pub mod memcx;
pub mod memcxt;
pub mod misc;
#[cfg(feature = "cshim")]
pub mod namespace;
pub mod nodes;
pub mod nullable;
pub mod pg_catalog;
pub mod pgbox;
pub mod rel;
pub mod shmem;
pub mod spi;
#[cfg(feature = "cshim")]
pub mod spinlock;
pub mod stringinfo;
pub mod trigger_support;
pub mod tupdesc;
pub mod varlena;
pub mod wrappers;
pub mod xid;

/// Not ready for public exposure.
mod ptr;
mod slice;
mod toast;

pub use aggregate::*;
pub use atomics::*;
pub use callbacks::*;
pub use datum::{
    numeric, AnyArray, AnyElement, AnyNumeric, Array, FromDatum, Inet, Internal, IntoDatum, Json,
    JsonB, Numeric, Range, Uuid, VariadicArray,
};
pub use enum_helper::*;
pub use fcinfo::*;
pub use guc::*;
#[cfg(feature = "cshim")]
#[allow(deprecated)]
pub use hooks::*;
pub use htup::*;
pub use inoutfuncs::*;
#[cfg(feature = "cshim")]
pub use list::old_list::*;
pub use lwlock::*;
pub use memcxt::*;
#[cfg(feature = "cshim")]
pub use namespace::*;
pub use nodes::*;
pub use pgbox::*;
pub use rel::*;
pub use shmem::*;
pub use spi::Spi; // only Spi.  We don't want the top-level namespace polluted with spi::Result and spi::Error
pub use stringinfo::*;
pub use trigger_support::*;
pub use tupdesc::*;
pub use varlena::*;
pub use wrappers::*;
pub use xid::*;

pub mod pg_sys;

// and re-export these
pub use pg_sys::elog::PgLogLevel;
pub use pg_sys::errcodes::PgSqlErrorCode;
pub use pg_sys::oids::PgOid;
pub use pg_sys::panic::pgrx_extern_c_guard;
pub use pg_sys::pg_try::PgTryBuilder;
pub use pg_sys::utils::name_data_to_str;
pub use pg_sys::PgBuiltInOids;
pub use pg_sys::{
    check_for_interrupts, debug1, debug2, debug3, debug4, debug5, ereport, error, function_name,
    info, log, notice, warning, FATAL, PANIC,
};
#[doc(hidden)]
pub use pgrx_sql_entity_graph;

mod seal {
    /// A trait you can reference but can't impl externally
    pub trait Sealed {}
}

// Postgres v15+ has the concept of an ABI "name".  The default is `c"PostgreSQL"` and this is the
// ABI that pgrx extensions expect to be running under.  We will refuse to compile if it is detected
// that we're trying to be built against some other kind of "postgres" that has its own ABI name.
//
// Unless the compiling user explicitly told us that they're aware of this via `--features unsafe-postgres`.
#[cfg(all(
    any(feature = "pg15", feature = "pg16", feature = "pg17"),
    not(feature = "unsafe-postgres")
))]
const _: () = {
    use core::ffi::CStr;
    // to appease `const`
    const fn same_cstr(a: &CStr, b: &CStr) -> bool {
        if a.to_bytes().len() != b.to_bytes().len() {
            return false;
        }
        let mut i = 0;
        while i < a.to_bytes().len() {
            if a.to_bytes()[i] != b.to_bytes()[i] {
                return false;
            }
            i += 1;
        }
        true
    }
    assert!(
        same_cstr(pg_sys::FMGR_ABI_EXTRA, c"PostgreSQL"),
        "Unsupported Postgres ABI. Perhaps you need `--features unsafe-postgres`?",
    );
};

/// A macro for marking a library compatible with [`pgrx`][crate].
///
/// <div class="example-wrap" style="display:inline-block">
/// <pre class="ignore" style="white-space:normal;font:inherit;">
///
/// **Note**: Every [`pgrx`][crate] extension **must** have this macro called at top level (usually `src/lib.rs`) to be valid.
///
/// </pre></div>
///
/// This calls [`pg_magic_func!()`](pg_magic_func).
#[macro_export]
macro_rules! pg_module_magic {
    () => {
        $crate::pg_magic_func!();

        // A marker function which must exist in the root of the extension for proper linking by the
        // "pgrx_embed" binary during `cargo-pgrx schema` generation.
        #[inline(never)] /* we don't want DCE to remove this as it *could* cause the compiler to decide to not link to us */
        #[doc(hidden)]
        pub fn __pgrx_marker() {
            // noop
        }
    };
}

/// Create the `Pg_magic_func` required by PGRX in extensions.
///
/// <div class="example-wrap" style="display:inline-block">
/// <pre class="ignore" style="white-space:normal;font:inherit;">
///
/// **Note**: Generally [`pg_module_magic`] is preferred, and results in this macro being called.
/// This macro should only be directly called in advanced use cases.
///
/// </pre></div>
///
/// Creates a “magic block” that describes the capabilities of the extension to
/// Postgres at runtime. From the [Dynamic Loading] section of the upstream documentation:
///
/// > To ensure that a dynamically loaded object file is not loaded into an incompatible
/// > server, PostgreSQL checks that the file contains a “magic block” with the appropriate
/// > contents. This allows the server to detect obvious incompatibilities, such as code
/// > compiled for a different major version of PostgreSQL. To include a magic block,
/// > write this in one (and only one) of the module source files, after having included
/// > the header `fmgr.h`:
/// >
/// > ```c
/// > PG_MODULE_MAGIC;
/// > ```
///
/// ## Acknowledgements
///
/// This macro was initially inspired from the `pg_module` macro by [Daniel Fagnan]
/// and expanded by [Benjamin Fry].
///
/// [Benjamin Fry]: https://github.com/bluejekyll/pg-extend-rs
/// [Daniel Fagnan]: https://github.com/thehydroimpulse/postgres-extension.rs
/// [Dynamic Loading]: https://www.postgresql.org/docs/current/xfunc-c.html#XFUNC-C-DYNLOAD
#[macro_export]
macro_rules! pg_magic_func {
    () => {
        #[no_mangle]
        #[allow(non_snake_case, unexpected_cfgs)]
        #[doc(hidden)]
        pub extern "C-unwind" fn Pg_magic_func() -> &'static ::pgrx::pg_sys::Pg_magic_struct {
            static MY_MAGIC: ::pgrx::pg_sys::Pg_magic_struct = ::pgrx::pg_sys::Pg_magic_struct {
                len: ::core::mem::size_of::<::pgrx::pg_sys::Pg_magic_struct>() as i32,
                version: ::pgrx::pg_sys::PG_VERSION_NUM as i32 / 100,
                funcmaxargs: ::pgrx::pg_sys::FUNC_MAX_ARGS as i32,
                indexmaxkeys: ::pgrx::pg_sys::INDEX_MAX_KEYS as i32,
                namedatalen: ::pgrx::pg_sys::NAMEDATALEN as i32,
                float8byval: cfg!(target_pointer_width = "64") as i32,
                #[cfg(any(feature = "pg15", feature = "pg16", feature = "pg17"))]
                abi_extra: {
                    // we'll use what the bindings tell us, but if it ain't "PostgreSQL" then we'll
                    // raise a compilation error unless the `unsafe-postgres` feature is set
                    let magic = ::pgrx::pg_sys::FMGR_ABI_EXTRA.to_bytes_with_nul();
                    let mut abi = [0 as ::pgrx::ffi::c_char; 32];
                    let mut i = 0;
                    while i < magic.len() {
                        abi[i] = magic[i] as ::pgrx::ffi::c_char;
                        i += 1;
                    }
                    abi
                },
            };

            // since Postgres calls this first, register our panic handler now
            // so we don't unwind into C / Postgres
            ::pgrx::pg_sys::panic::register_pg_guard_panic_hook();

            // return the magic
            &MY_MAGIC
        }
    };
}

pub(crate) static UTF8DATABASE: Lazy<Utf8Compat> = Lazy::new(|| {
    use pg_sys::pg_enc::*;
    let encoding_int = unsafe { pg_sys::GetDatabaseEncoding() };
    match encoding_int as _ {
        PG_UTF8 => Utf8Compat::Yes,
        // The 0 encoding. It... may be UTF-8
        PG_SQL_ASCII => Utf8Compat::Maybe,
        // Modifies ASCII, and should never be seen as PG doesn't support it as server encoding
        PG_SJIS | PG_SHIFT_JIS_2004
        // Not specified as an ASCII extension, also not a server encoding
        | PG_BIG5
        // Wild vendor differences including non-ASCII are possible, also not a server encoding
        | PG_JOHAB => unreachable!("impossible? unsupported non-ASCII-compatible database encoding is not a server encoding"),
        // Other Postgres encodings either extend US-ASCII or CP437 (which includes US-ASCII)
        // There may be a subtlety that requires us to revisit this later
        1..=41=> Utf8Compat::Ascii,
        // Unfamiliar encoding? Run UTF-8 validation like normal and hope for the best
        _ => Utf8Compat::Maybe,
    }
});

#[derive(Debug, Clone, Copy)]
pub(crate) enum Utf8Compat {
    /// It's UTF-8, so... obviously
    Yes,
    /// This is what is assumed about "SQL_ASCII"
    Maybe,
    /// An "extended ASCII" encoding, so we're fine if we only touch ASCII
    Ascii,
}

/// Entry point for cargo-pgrx's schema generation so that PGRX's framework can
/// generate SQL for its types and functions and topographically sort them into
/// an order Postgres will accept. Typically written by the `cargo pgrx new`
/// template, so you probably don't need to worry about this.
#[macro_export]
macro_rules! pgrx_embed {
    () => {
        mod pgrx_embed {
            #![allow(unexpected_cfgs)]

            #[cfg(not(pgrx_embed))]
            pub fn main() {
                panic!("PGRX_EMBED was not set.");
            }
            #[cfg(pgrx_embed)]
            include!(env!("PGRX_EMBED"));
        }
        pub use pgrx_embed::main;
    };
}
