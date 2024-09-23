use std::sync::Arc;

use arrow::array::{ArrayRef, Decimal128Array, ListArray};
use pgrx::AnyNumeric;

use crate::{
    arrow_parquet::{arrow_utils::arrow_array_offsets, pg_to_arrow::PgTypeToArrowArray},
    type_compat::pg_arrow_type_conversions::numeric_to_i128,
};

use super::PgToArrowAttributeContext;

// Numeric
impl PgTypeToArrowArray<AnyNumeric> for Vec<Option<AnyNumeric>> {
    fn to_arrow_array(self, context: &PgToArrowAttributeContext) -> ArrayRef {
        let precision = context
            .precision
            .expect("precision is required in context for numeric");
        let scale = context
            .scale
            .expect("scale is required in context for numeric");

        let numerics = self
            .into_iter()
            .map(|numeric| numeric.map(numeric_to_i128))
            .collect::<Vec<_>>();

        let numeric_array = Decimal128Array::from(numerics)
            .with_precision_and_scale(precision as _, scale as _)
            .unwrap_or_else(|e| panic!("failed to create Decimal128Array: {}", e));

        Arc::new(numeric_array)
    }
}

// Numeric[]
impl PgTypeToArrowArray<AnyNumeric> for Vec<Option<Vec<Option<AnyNumeric>>>> {
    fn to_arrow_array(self, context: &PgToArrowAttributeContext) -> ArrayRef {
        let (offsets, nulls) = arrow_array_offsets(&self);

        // gets rid of the first level of Option, then flattens the inner Vec<Option<bool>>.
        let pg_array = self
            .into_iter()
            .flatten()
            .flatten()
            .map(|numeric| numeric.map(numeric_to_i128))
            .collect::<Vec<_>>();

        let precision = context
            .precision
            .expect("precision is required in context for numeric");
        let scale = context
            .scale
            .expect("scale is required in context for numeric");

        let numeric_array = Decimal128Array::from(pg_array)
            .with_precision_and_scale(precision as _, scale as _)
            .unwrap_or_else(|e| panic!("failed to create Decimal128Array: {}", e));

        let list_array = ListArray::new(
            context.field.clone(),
            offsets,
            Arc::new(numeric_array),
            Some(nulls),
        );

        Arc::new(list_array)
    }
}
