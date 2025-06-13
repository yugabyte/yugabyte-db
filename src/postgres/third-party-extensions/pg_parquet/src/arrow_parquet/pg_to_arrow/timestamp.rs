use std::sync::Arc;

use arrow::array::{ArrayRef, ListArray, TimestampMicrosecondArray};
use pgrx::datum::Timestamp;

use crate::{
    arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray},
    type_compat::pg_arrow_type_conversions::timestamp_to_i64,
};

use super::PgToArrowAttributeContext;

// Timestamp
impl PgTypeToArrowArray<Timestamp> for Vec<Option<Timestamp>> {
    fn to_arrow_array(self, _context: &PgToArrowAttributeContext) -> ArrayRef {
        let timestamps = self
            .into_iter()
            .map(|timestamp| timestamp.map(timestamp_to_i64))
            .collect::<Vec<_>>();
        let timestamp_array = TimestampMicrosecondArray::from(timestamps);
        Arc::new(timestamp_array)
    }
}

// Timestamp[]
impl PgTypeToArrowArray<Timestamp> for Vec<Option<Vec<Option<Timestamp>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self
            .into_iter()
            .flatten()
            .flatten()
            .map(|timestamp| timestamp.map(timestamp_to_i64))
            .collect::<Vec<_>>();

        let timestamp_array = TimestampMicrosecondArray::from(pg_array);

        let list_array = ListArray::new(
            element_context.field(),
            offsets,
            Arc::new(timestamp_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
