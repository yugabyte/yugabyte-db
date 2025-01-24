use std::sync::Arc;

use arrow::array::{ArrayRef, Int32Array, ListArray};

use crate::arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray};

use super::PgToArrowAttributeContext;

// Int32
impl PgTypeToArrowArray<i32> for Vec<Option<i32>> {
    fn to_arrow_array(self, _context: &PgToArrowAttributeContext) -> ArrayRef {
        let int32_array = Int32Array::from(self);
        Arc::new(int32_array)
    }
}

// Int32[]
impl PgTypeToArrowArray<i32> for Vec<Option<Vec<Option<i32>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self.into_iter().flatten().flatten().collect::<Vec<_>>();

        let int32_array = Int32Array::from(pg_array);

        let list_array = ListArray::new(
            element_context.field(),
            offsets,
            Arc::new(int32_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
