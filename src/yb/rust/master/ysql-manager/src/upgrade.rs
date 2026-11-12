// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

//! YSQL major upgrades: what C++'s `YsqlInitDBAndMajorUpgradeHandler` keeps of the catalog's
//! YSQL major version.

use master_catalog_state::defaults::YSQL_CATALOG_CONFIG_ID;
use master_catalog_state::CatalogSnapshot;
use yb_proto::pb::catalog_entity_info::SysConfigEntryPB;
use yb_proto::pb::master_types::SysRowEntryType;

/// The catalog's YSQL major version: `ysql_major_catalog_upgrade_info.catalog_version`, or 11 for
/// a catalog without it (the field came with PG15). None without a YSQL catalog config.
pub fn catalog_major_version(snapshot: &CatalogSnapshot) -> Option<u32> {
    let config: SysConfigEntryPB = snapshot
        .get(SysRowEntryType::SYS_CONFIG, YSQL_CATALOG_CONFIG_ID)?
        .parse()
        .ok()?;
    let ysql = config.ysql_catalog_config();
    Some(
        ysql.ysql_major_catalog_upgrade_info
            .as_ref()
            .map_or(11, |info| info.catalog_version()),
    )
}

/// `YsqlManager::IsMajorUpgradeInProgress`: the catalog is on another YSQL major version than this
/// build (`build_major_version`, None when YSQL is off).
pub fn major_upgrade_in_progress(
    snapshot: &CatalogSnapshot,
    build_major_version: Option<u32>,
) -> bool {
    match (build_major_version, catalog_major_version(snapshot)) {
        (Some(build), Some(catalog)) => build != catalog,
        _ => false,
    }
}
