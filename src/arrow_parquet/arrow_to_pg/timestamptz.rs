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
            let timezone = context
                .timezone
                .as_ref()
                .expect("timezone is required for timestamptz");

            Some(i64_to_timestamptz(self.value(0), timezone))
        }
    }
}

// Timestamptz[]
impl ArrowArrayToPgType<Vec<Option<TimestampWithTimeZone>>> for TimestampMicrosecondArray {
    fn to_pg_type(
        self,
        context: &ArrowToPgAttributeContext,
    ) -> Option<Vec<Option<TimestampWithTimeZone>>> {
        let mut vals = vec![];

        let timezone = context
            .timezone
            .as_ref()
            .expect("timezone is required for timestamptz[]");

        for val in self.iter() {
            let val = val.map(|v| i64_to_timestamptz(v, timezone));
            vals.push(val);
        }
        Some(vals)
    }
}
