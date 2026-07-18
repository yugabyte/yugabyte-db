use arrow::array::{Array, TimestampMicrosecondArray};
use pgrx::datum::TimestampWithTimeZone;

use crate::type_compat::pg_arrow_type_conversions::i64_to_timestamptz;

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Timestamptz
impl ArrowArrayToPgType<TimestampWithTimeZone> for TimestampMicrosecondArray {
    fn to_pg_type(self, context: &ArrowToPgAttributeContext) -> Option<TimestampWithTimeZone> {
        if self.is_null(0) {
            None
        } else {
            Some(i64_to_timestamptz(self.value(0), context.timezone()))
        }
    }
}

// Timestamptz[]
impl ArrowArrayToPgType<Vec<Option<TimestampWithTimeZone>>> for TimestampMicrosecondArray {
    fn to_pg_type(
        self,
        element_context: &ArrowToPgAttributeContext,
    ) -> Option<Vec<Option<TimestampWithTimeZone>>> {
        let mut vals = vec![];

        let timezone = element_context.timezone();

        for val in self.iter() {
            let val = val.map(|v| i64_to_timestamptz(v, timezone));
            vals.push(val);
        }
        Some(vals)
    }
}
