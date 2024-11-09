use arrow::array::{Array, Decimal128Array};
use pgrx::AnyNumeric;

use crate::type_compat::pg_arrow_type_conversions::i128_to_numeric;

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Numeric
impl ArrowArrayToPgType<AnyNumeric> for Decimal128Array {
    fn to_pg_type(self, context: &ArrowToPgAttributeContext) -> Option<AnyNumeric> {
        if self.is_null(0) {
            None
        } else {
            let precision = context.precision.expect("Expected precision");
            let scale = context.scale.expect("Expected scale");

            Some(i128_to_numeric(
                self.value(0),
                precision,
                scale,
                context.typmod,
            ))
        }
    }
}

// Numeric[]
impl ArrowArrayToPgType<Vec<Option<AnyNumeric>>> for Decimal128Array {
    fn to_pg_type(self, context: &ArrowToPgAttributeContext) -> Option<Vec<Option<AnyNumeric>>> {
        let precision = context.precision.expect("Expected precision");
        let scale = context.scale.expect("Expected scale");

        let mut vals = vec![];
        for val in self.iter() {
            let val = val.map(|v| i128_to_numeric(v, precision, scale, context.typmod));
            vals.push(val);
        }
        Some(vals)
    }
}
