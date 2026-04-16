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
            Some(i128_to_numeric(
                self.value(0),
                context.precision(),
                context.scale(),
                context.typmod(),
            ))
        }
    }
}

// Numeric[]
impl ArrowArrayToPgType<Vec<Option<AnyNumeric>>> for Decimal128Array {
    fn to_pg_type(
        self,
        element_context: &ArrowToPgAttributeContext,
    ) -> Option<Vec<Option<AnyNumeric>>> {
        let mut vals = vec![];
        for val in self.iter() {
            let val = val.map(|v| {
                i128_to_numeric(
                    v,
                    element_context.precision(),
                    element_context.scale(),
                    element_context.typmod(),
                )
            });
            vals.push(val);
        }
        Some(vals)
    }
}
