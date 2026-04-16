//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#[cfg(
    // no features at all will cause problems
    not(any(feature = "pg13", feature = "pg14", feature = "pg15", feature = "pg16", feature = "pg17")),
)]
std::compile_error!("exactly one feature must be provided (pg13, pg14, pg15, pg16, pg17)");

mod cshim;
mod cstr;
mod include;
mod node;
mod port;
pub mod submodules;

#[cfg(feature = "cshim")]
pub use cshim::*;

pub use cstr::AsPgCStr;
pub use include::*;
pub use node::PgNode;
pub use port::*;
pub use submodules::*;

mod seal {
    pub trait Sealed {}
}

// Hack to fix linker errors that we get under amazonlinux2 on some PG versions
// due to our wrappers for various system library functions. Should be fairly
// harmless, but ideally we would not wrap these functions
// (https://github.com/pgcentralfoundation/pgrx/issues/730).
#[cfg(target_os = "linux")]
#[link(name = "resolv")]
extern "C" {}
