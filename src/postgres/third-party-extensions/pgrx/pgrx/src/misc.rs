//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use std::hash::{Hash, Hasher};

/// wrapper around `SeaHasher` from [Seahash](https://crates.io/crates/seahash)
///
/// Primarily used by `pgrx`'s `#[derive(PostgresHash)]` macro.
pub fn pgrx_seahash<T: Hash>(value: &T) -> u64 {
    // taken from sources of "SeaHasher, v4.0.1" [Seahash](https://crates.io/crates/seahash)
    // assuming the underlying implementation doesn't change, we
    // also want to ensure however we seed it doesn't change either
    //
    // these hash values might be stored on disk by Postgres, so we can't afford
    // to have them changing over time
    let mut hasher = seahash::SeaHasher::with_seeds(
        0x16f11fe89b0d677c,
        0xb480a793d8e6c86c,
        0x6fe2e5aaf078ebc9,
        0x14f994a4c5259381,
    );
    value.hash(&mut hasher);
    hasher.finish()
}
