//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, ReturnsError, ReturnsRef, SqlMappingRef, SqlTranslatable,
};

use crate::{AnyNumeric, Numeric};

const fn numeric_precision(precision: u32) -> Option<u32> {
    if precision == 0 { None } else { Some(precision) }
}

const fn numeric_scale(precision: u32, scale: u32) -> Option<u32> {
    if precision == 0 || scale == 0 { None } else { Some(scale) }
}

unsafe impl<const P: u32, const S: u32> SqlTranslatable for Numeric<P, S> {
    const TYPE_IDENT: &'static str = crate::pgrx_resolved_type!(Numeric<P, S>);
    const TYPE_ORIGIN: pgrx_sql_entity_graph::metadata::TypeOrigin =
        pgrx_sql_entity_graph::metadata::TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> =
        Ok(SqlMappingRef::Numeric { precision: numeric_precision(P), scale: numeric_scale(P, S) });
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::Numeric {
            precision: numeric_precision(P),
            scale: numeric_scale(P, S),
        }));
}

unsafe impl SqlTranslatable for AnyNumeric {
    const TYPE_IDENT: &'static str = crate::pgrx_resolved_type!(AnyNumeric);
    const TYPE_ORIGIN: pgrx_sql_entity_graph::metadata::TypeOrigin =
        pgrx_sql_entity_graph::metadata::TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> =
        Ok(SqlMappingRef::Numeric { precision: None, scale: None });
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::Numeric { precision: None, scale: None }));
}
