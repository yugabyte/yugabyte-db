//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx::prelude::*;
use pgrx::StringInfo;
use serde::{Deserialize, Serialize};
use std::ffi::CStr;
use std::fmt::{Display, Formatter, Write};

/// standard Rust equality/comparison derives
#[derive(Eq, PartialEq, Ord, Hash, PartialOrd)]

/// Support using this struct as a Postgres type, which the easy way requires Serde
#[derive(PostgresType, Serialize, Deserialize)]

/// automatically generate =, <> SQL operator functions
#[derive(PostgresEq)]

/// automatically generate <, >, <=, >=, and a "_cmp" SQL functions
/// When "PostgresEq" is also derived, pgrx also creates an "opclass" (and family)
/// so that the type can be used in indexes `USING btree`
#[derive(PostgresOrd)]

/// automatically generate a "_hash" function, and the necessary "opclass" (and family)
/// so the type can also be used in indexes `USING hash`
#[derive(PostgresHash)]

/// Necessary derives for generally working with [`PgVarlena`]
#[derive(Copy, Clone)]

/// For unit tests
#[derive(Debug)]

/// we want this to look like a C struct
#[repr(C)]

/// Indicate to `#[derive(PostgresType)]` that we'll write our own input and output function
#[pgvarlena_inoutfuncs]
pub struct PgVarlenaThing {
    a: u64,
    b: u64,
    c: i32,
    d: [u8; 5],
}

impl PgVarlenaThing {
    #[allow(dead_code)]
    pub fn new(a: u64, b: u64, c: i32, d: [u8; 5]) -> Self {
        Self { a, b, c, d }
    }
}

impl Display for PgVarlenaThing {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "{};{};{};{:?}", self.a, self.b, self.c, self.d)
    }
}

// implement function to parse string representation of this type and a function to convert it
// to a string
impl PgVarlenaInOutFuncs for PgVarlenaThing {
    fn input(input: &CStr) -> PgVarlena<Self>
    where
        Self: Copy + Sized,
    {
        let s = input.to_str().unwrap();
        let mut result: PgVarlena<PgVarlenaThing> = PgVarlena::new();

        // parsing a string in this format:  1;2;3;[1,2,3,4,5]

        let mut parts = s.split(';');
        result.a = parts.next().unwrap().parse().unwrap();
        result.b = parts.next().unwrap().parse().unwrap();
        result.c = parts.next().unwrap().parse().unwrap();

        let d = parts.next().unwrap();
        let d = d.trim_start_matches('[');
        let d = d.trim_end_matches(']');
        let d = d.split(',').map(|e| e.parse().unwrap()).collect::<Vec<u8>>();
        result.d = [d[0], d[1], d[2], d[3], d[4]];
        result
    }

    fn output(&self, buffer: &mut StringInfo) {
        write!(buffer, "{self}").unwrap()
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::pgvarlena::PgVarlenaThing;
    use pgrx::prelude::*;
    use std::error::Error;

    #[pg_test]
    fn test_varlena_thing() -> Result<(), Box<dyn Error>> {
        let thing = Spi::get_one::<PgVarlena<PgVarlenaThing>>(
            "SELECT '1;2;3;[1,2,3,4,5]'::pgvarlenathing",
        )?
        .unwrap();

        assert_eq!(thing.as_ref(), &PgVarlenaThing::new(1, 2, 3, [1, 2, 3, 4, 5]));

        let lt = Spi::get_one::<bool>(
            "SELECT '1;2;3;[1,2,3,4,5]'::pgvarlenathing < '2;2;3;[1,2,3,4,5]'::pgvarlenathing",
        )?
        .unwrap();
        assert!(lt);

        let gt = Spi::get_one::<bool>(
            "SELECT '2;2;3;[1,2,3,4,5]'::pgvarlenathing > '1;2;3;[1,2,3,4,5]'::pgvarlenathing",
        )?
        .unwrap();
        assert!(gt);

        let eq = Spi::get_one::<bool>(
            "SELECT '1;2;3;[1,2,3,4,5]'::pgvarlenathing = '1;2;3;[1,2,3,4,5]'::pgvarlenathing",
        )?
        .unwrap();
        assert!(eq);

        Ok(())
    }
}
