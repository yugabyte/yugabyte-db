use std::sync::Arc;

use arrow::array::{ArrayRef, BinaryArray, ListArray};

use crate::{
    arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray},
    type_compat::geometry::Geometry,
};

use super::PgToArrowAttributeContext;

// Geometry
impl PgTypeToArrowArray<Geometry> for Vec<Option<Geometry>> {
    fn to_arrow_array(self, _context: &PgToArrowAttributeContext) -> ArrayRef {
        let wkbs = self
            .iter()
            .map(|geometry| geometry.as_deref())
            .collect::<Vec<_>>();
        let wkb_array = BinaryArray::from(wkbs);
        Arc::new(wkb_array)
    }
}

// Geometry[]
impl PgTypeToArrowArray<Geometry> for Vec<Option<Vec<Option<Geometry>>>> {
    fn to_arrow_array(self, element_context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self.into_iter().flatten().flatten().collect::<Vec<_>>();

        let wkbs = pg_array
            .iter()
            .map(|geometry| geometry.as_deref())
            .collect::<Vec<_>>();

        let wkb_array = BinaryArray::from(wkbs);

        let list_array = ListArray::new(
            element_context.field(),
            offsets,
            Arc::new(wkb_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
