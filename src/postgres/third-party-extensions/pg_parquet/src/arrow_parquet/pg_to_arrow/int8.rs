use std::sync::Arc;

use arrow::array::{ArrayRef, Int64Array, ListArray};

use crate::arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray};

use super::PgToArrowAttributeContext;

// Int64
impl PgTypeToArrowArray<i64> for Vec<Option<i64>> {
    fn to_arrow_array(self, _context: &PgToArrowAttributeContext) -> ArrayRef {
        let int64_array = Int64Array::from(self);
        Arc::new(int64_array)
    }
}

// Int64[]
impl PgTypeToArrowArray<i64> for Vec<Option<Vec<Option<i64>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self.into_iter().flatten().flatten().collect::<Vec<_>>();

        let int64_array = Int64Array::from(pg_array);

        let list_array = ListArray::new(
            element_context.field(),
            offsets,
            Arc::new(int64_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
