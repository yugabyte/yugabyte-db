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
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};

#[cfg(any(
    feature = "pg17",
    feature = "pg16",
    feature = "pg15",
    feature = "pg14",
    feature = "pg13",
))]
unsafe impl SqlTranslatable for crate::FunctionCallInfoBaseData {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::Skip)
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::Skip))
    }
}

unsafe impl SqlTranslatable for crate::PlannerInfo {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("internal"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("internal")))
    }
}

unsafe impl SqlTranslatable for crate::IndexAmRoutine {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("internal"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("internal")))
    }
}

unsafe impl SqlTranslatable for crate::TableAmRoutine {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("internal"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("internal")))
    }
}

unsafe impl SqlTranslatable for crate::FdwRoutine {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("fdw_handler"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("fdw_handler")))
    }
}

unsafe impl SqlTranslatable for crate::BOX {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("box"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("box")))
    }
}

unsafe impl SqlTranslatable for crate::Point {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("point"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("point")))
    }
}

unsafe impl SqlTranslatable for crate::ItemPointerData {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("tid"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("tid")))
    }
}

unsafe impl SqlTranslatable for crate::Datum {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Err(ArgumentError::Datum)
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Err(ReturnsError::Datum)
    }
}
