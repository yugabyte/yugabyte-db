//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
mod aggregate_tests;
mod anyarray_tests;
mod anyelement_tests;
mod anynumeric_tests;
mod array_tests;
mod attributes_tests;
mod bgworker_tests;
#[cfg(feature = "cshim")]
mod bindings_of_inline_fn_tests;
mod borrow_datum;
mod bytea_tests;
mod cfg_tests;
mod complex;
mod composite_type_tests;
mod datetime_tests;
mod default_arg_value_tests;
mod derive_pgtype_lifetimes;
mod enum_type_tests;
mod fcinfo_tests;
mod fn_call_tests;
mod from_into_datum_tests;
mod geo_tests;
mod guc_tests;
mod heap_tuple;
#[cfg(feature = "cshim")]
mod hooks_tests;
mod inet_tests;
mod internal_tests;
mod issue1134;
mod json_tests;
mod lifetime_tests;
mod list_tests;
mod log_tests;
mod memcxt_tests;
mod name_tests;
mod numeric_tests;
mod oid_tests;
mod pg_cast_tests;
mod pg_extern_tests;
mod pg_guard_tests;
mod pg_operator_tests;
mod pg_try_tests;
mod pgbox_tests;
mod pgrx_module_qualification;
mod postgres_type_tests;
#[cfg(feature = "proptest")]
mod proptests;
mod range_tests;
mod rel_tests;
mod result_tests;
mod roundtrip_tests;
mod schema_tests;
mod shmem_tests;
mod spi_tests;
mod srf_tests;
mod struct_type_tests;
mod trigger_tests;
mod uuid_tests;
mod variadic_tests;
mod xact_callback_tests;
mod xid64_tests;
mod zero_datum_edge_cases;

use complex::Complex;
