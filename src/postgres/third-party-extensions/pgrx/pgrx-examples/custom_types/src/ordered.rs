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
use serde::{Deserialize, Serialize};
use std::cmp::Ordering;

// Demonstrates that Postgres-defined ordering works.
#[derive(
    Serialize,
    Deserialize,
    Debug,
    Clone,
    Eq,
    PartialEq,
    PostgresType,
    PostgresEq,
    PostgresOrd
)]
pub struct OrderedThing {
    item: String,
}

// A silly yet consistent ordering. Strings which start with lowercase letters are sorted normally,
// but strings which start with non-lowercase letters are sorted backwards.
impl Ord for OrderedThing {
    fn cmp(&self, other: &Self) -> Ordering {
        fn starts_lower(thing: &OrderedThing) -> bool {
            thing.item.chars().next().map_or(false, |c| c.is_lowercase())
        }
        if starts_lower(self) {
            if starts_lower(other) {
                self.item.cmp(&other.item)
            } else {
                std::cmp::Ordering::Greater
            }
        } else if starts_lower(other) {
            std::cmp::Ordering::Less
        } else {
            self.item.cmp(&other.item).reverse()
        }
    }
}

impl PartialOrd for OrderedThing {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use crate::ordered::OrderedThing;
    use pgrx::prelude::*;

    #[cfg(not(feature = "no-schema-generation"))]
    #[pg_test]
    fn test_ordering_via_spi() {
        let items = Spi::get_one::<Vec<OrderedThing>>(
            "SELECT array_agg(i ORDER BY i) FROM (VALUES \
                ('{\"item\":\"foo\"}'::OrderedThing), \
                ('{\"item\":\"bar\"}'::OrderedThing), \
                ('{\"item\":\"Foo\"}'::OrderedThing), \
                ('{\"item\":\"Bar\"}'::OrderedThing))\
                items(i);",
        );

        assert_eq!(
            items,
            Ok(Some(vec![
                OrderedThing { item: "Foo".to_string() },
                OrderedThing { item: "Bar".to_string() },
                OrderedThing { item: "bar".to_string() },
                OrderedThing { item: "foo".to_string() },
            ]))
        );
    }
}
