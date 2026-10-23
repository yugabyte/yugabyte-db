//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::{
    BOX, CIRCLE, Datum, FdwRoutine, FunctionCallInfoBaseData, IndexAmRoutine, ItemPointerData,
    PlannerInfo, Point, TableAmRoutine,
};
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, ReturnsError, ReturnsRef, SqlMappingRef, SqlTranslatable, TypeOrigin,
};

unsafe impl SqlTranslatable for FunctionCallInfoBaseData {
    const TYPE_IDENT: &'static str =
        pgrx_sql_entity_graph::pgrx_resolved_type!(FunctionCallInfoBaseData);
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = Ok(SqlMappingRef::Skip);
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> = Ok(ReturnsRef::One(SqlMappingRef::Skip));
}

unsafe impl SqlTranslatable for PlannerInfo {
    const TYPE_IDENT: &'static str = pgrx_sql_entity_graph::pgrx_resolved_type!(PlannerInfo);
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> =
        Ok(SqlMappingRef::literal("internal"));
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::literal("internal")));
}

unsafe impl SqlTranslatable for IndexAmRoutine {
    const TYPE_IDENT: &'static str = pgrx_sql_entity_graph::pgrx_resolved_type!(IndexAmRoutine);
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> =
        Ok(SqlMappingRef::literal("internal"));
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::literal("internal")));
}

unsafe impl SqlTranslatable for TableAmRoutine {
    const TYPE_IDENT: &'static str = pgrx_sql_entity_graph::pgrx_resolved_type!(TableAmRoutine);
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> =
        Ok(SqlMappingRef::literal("internal"));
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::literal("internal")));
}

unsafe impl SqlTranslatable for FdwRoutine {
    const TYPE_IDENT: &'static str = pgrx_sql_entity_graph::pgrx_resolved_type!(FdwRoutine);
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> =
        Ok(SqlMappingRef::literal("fdw_handler"));
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::literal("fdw_handler")));
}

unsafe impl SqlTranslatable for BOX {
    const TYPE_IDENT: &'static str = pgrx_sql_entity_graph::pgrx_resolved_type!(BOX);
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = Ok(SqlMappingRef::literal("box"));
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::literal("box")));
}

unsafe impl SqlTranslatable for CIRCLE {
    const TYPE_IDENT: &'static str = pgrx_sql_entity_graph::pgrx_resolved_type!(CIRCLE);
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = Ok(SqlMappingRef::literal("circle"));
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::literal("circle")));
}

unsafe impl SqlTranslatable for Point {
    const TYPE_IDENT: &'static str = pgrx_sql_entity_graph::pgrx_resolved_type!(Point);
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = Ok(SqlMappingRef::literal("point"));
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::literal("point")));
}

unsafe impl SqlTranslatable for ItemPointerData {
    const TYPE_IDENT: &'static str = pgrx_sql_entity_graph::pgrx_resolved_type!(ItemPointerData);
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = Ok(SqlMappingRef::literal("tid"));
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::literal("tid")));
}

unsafe impl SqlTranslatable for Datum {
    const TYPE_IDENT: &'static str = pgrx_sql_entity_graph::pgrx_resolved_type!(Datum);
    const TYPE_ORIGIN: TypeOrigin = TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = Err(ArgumentError::Datum);
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> = Err(ReturnsError::Datum);
}
