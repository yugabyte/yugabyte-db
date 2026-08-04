//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use core::fmt::Formatter;
use std::fmt;

use serde::de::Visitor;
use serde::{Deserialize, Deserializer, Serialize, Serializer};

use crate::AnyNumeric;

impl Serialize for AnyNumeric {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serializer.collect_str(&format_args!("{self}"))
    }
}

impl<'de> Deserialize<'de> for AnyNumeric {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct NumericVisitor;

        impl<'de> Visitor<'de> for NumericVisitor {
            type Value = AnyNumeric;

            fn expecting(&self, formatter: &mut Formatter) -> fmt::Result {
                formatter.write_str("a JSON number or a \"quoted JSON number\"")
            }

            fn visit_i64<E>(self, v: i64) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                AnyNumeric::try_from(v).map_err(E::custom)
            }

            #[inline]
            fn visit_u64<E>(self, v: u64) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                AnyNumeric::try_from(v).map_err(E::custom)
            }

            #[inline]
            fn visit_f64<E>(self, v: f64) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                AnyNumeric::try_from(v).map_err(E::custom)
            }

            #[inline]
            fn visit_str<E>(self, v: &str) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                AnyNumeric::try_from(v).map_err(E::custom)
            }
        }

        deserializer.deserialize_any(NumericVisitor)
    }
}
