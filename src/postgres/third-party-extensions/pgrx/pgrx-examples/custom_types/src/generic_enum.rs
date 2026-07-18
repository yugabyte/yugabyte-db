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
use serde::*;
use std::fmt::{Display, Formatter};

#[derive(PostgresEnum, Serialize)]
pub enum SomeValue {
    One,
    Two,
    Three,
    Four,
    Five,
}

impl Display for SomeValue {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), std::fmt::Error> {
        match self {
            SomeValue::One => write!(f, "1: one"),
            SomeValue::Two => write!(f, "2: two"),
            SomeValue::Three => write!(f, "3: three"),
            SomeValue::Four => write!(f, "4: four"),
            SomeValue::Five => write!(f, "5: five"),
        }
    }
}

#[pg_extern]
fn get_some_value_name(input: SomeValue) -> String {
    format!("{input}")
}
