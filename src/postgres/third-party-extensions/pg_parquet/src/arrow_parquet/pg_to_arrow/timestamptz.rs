use std::sync::Arc;

use arrow::array::{ArrayRef, ListArray, TimestampMicrosecondArray};
use pgrx::datum::TimestampWithTimeZone;

use crate::{
    arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray},
    type_compat::pg_arrow_type_conversions::timestamptz_to_i64,
};

use super::PgToArrowAttributeContext;

// TimestampTz
impl PgTypeToArrowArray<TimestampWithTimeZone> for Vec<Option<TimestampWithTimeZone>> {
    fn to_arrow_array(self, _context: &PgToArrowAttributeContext) -> ArrayRef {
        let timestamptzs = self
            .into_iter()
            .map(|timestamptz| timestamptz.map(timestamptz_to_i64))
            .collect::<Vec<_>>();
        let timestamptz_array = TimestampMicrosecondArray::from(timestamptzs).with_timezone_utc();
        Arc::new(timestamptz_array)
    }
}

// TimestampTz[]
impl PgTypeToArrowArray<TimestampWithTimeZone> for Vec<Option<Vec<Option<TimestampWithTimeZone>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self
            .into_iter()
            .flatten()
            .flatten()
            .map(|timestamptz| timestamptz.map(timestamptz_to_i64))
            .collect::<Vec<_>>();

        let timestamptz_array = TimestampMicrosecondArray::from(pg_array).with_timezone_utc();

        let list_array = ListArray::new(
            element_context.field(),
            offsets,
            Arc::new(timestamptz_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
