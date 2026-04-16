use std::sync::Arc;

use arrow::array::{ArrayRef, Date32Array, ListArray};
use pgrx::datum::Date;

use crate::{
    arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray},
    type_compat::pg_arrow_type_conversions::date_to_i32,
};

use super::PgToArrowAttributeContext;

// Date
impl PgTypeToArrowArray<Date> for Vec<Option<Date>> {
    fn to_arrow_array(self, _context: &PgToArrowAttributeContext) -> ArrayRef {
        let dates = self
            .into_iter()
            .map(|date| date.map(date_to_i32))
            .collect::<Vec<_>>();
        let date_array = Date32Array::from(dates);
        Arc::new(date_array)
    }
}

// Date[]
impl PgTypeToArrowArray<pgrx::Array<'_, Date>> for Vec<Option<Vec<Option<Date>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self
            .into_iter()
            .flatten()
            .flatten()
            .map(|d| d.map(date_to_i32))
            .collect::<Vec<_>>();

        let date_array = Date32Array::from(pg_array);

        let list_array = ListArray::new(
            element_context.field(),
            offsets,
            Arc::new(date_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
