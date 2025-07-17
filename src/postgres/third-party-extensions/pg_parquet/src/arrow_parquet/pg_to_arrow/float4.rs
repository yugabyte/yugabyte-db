use std::sync::Arc;

use arrow::array::{ArrayRef, Float32Array, ListArray};

use crate::arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray};

use super::PgToArrowAttributeContext;

// Float32
impl PgTypeToArrowArray<f32> for Vec<Option<f32>> {
    fn to_arrow_array(self, _context: &PgToArrowAttributeContext) -> ArrayRef {
        let float_array = Float32Array::from(self);
        Arc::new(float_array)
    }
}

// Float32[]
impl PgTypeToArrowArray<pgrx::Array<'_, f32>> for Vec<Option<Vec<Option<f32>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self.into_iter().flatten().flatten().collect::<Vec<_>>();

        let float_array = Float32Array::from(pg_array);

        let list_array = ListArray::new(
            element_context.field(),
            offsets,
            Arc::new(float_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
