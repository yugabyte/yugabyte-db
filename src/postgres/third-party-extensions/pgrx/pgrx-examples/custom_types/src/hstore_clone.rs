//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use maplit::*;
use pgrx::prelude::*;
use serde::*;
use std::collections::HashMap;

#[derive(PostgresType, Serialize, Deserialize, Debug, Eq, PartialEq)]
#[derive(Default)]
pub struct RustStore(HashMap<String, String>);

#[pg_extern]
fn rstore(key: String, value: String) -> RustStore {
    RustStore(hashmap!(key => value))
}

#[pg_extern]
fn rstore_put(rstore: Option<RustStore>, key: String, value: String) -> RustStore {
    let mut rstore = rstore.unwrap_or_default();
    rstore.0.insert(key, value);
    rstore
}

#[pg_extern]
fn rstore_get(rstore: Option<RustStore>, key: String) -> Option<String> {
    rstore.and_then(|rstore| rstore.0.get(&key).cloned())
}

#[pg_extern]
fn rstore_remove(rstore: Option<RustStore>, key: String) -> Option<RustStore> {
    match rstore {
        Some(mut rstore) => {
            rstore.0.remove(&key);

            if rstore.0.is_empty() {
                None
            } else {
                Some(rstore)
            }
        }
        None => None,
    }
}

#[pg_extern]
fn rstore_size(rstore: Option<RustStore>) -> i64 {
    rstore.map_or(0, |rstore| rstore.0.len()) as i64
}

#[pg_extern]
fn rstore_table(
    rstore: Option<RustStore>,
) -> TableIterator<'static, (name!(key, String), name!(value, String))> {
    match rstore {
        Some(rstore) => TableIterator::new(rstore.0),
        None => TableIterator::once((String::new(), String::new())),
    }
}
