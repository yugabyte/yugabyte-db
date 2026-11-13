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

pub use master_table_lifecycle::major_upgrade::{
    catalog_major_version, major_upgrade_in_progress, upgrade_info,
};
