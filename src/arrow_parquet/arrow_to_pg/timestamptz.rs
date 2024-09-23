use arrow::array::{Array, TimestampMicrosecondArray};
use pgrx::datum::TimestampWithTimeZone;

use crate::type_compat::pg_arrow_type_conversions::i64_to_timestamptz;

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Timestamptz
impl ArrowArrayToPgType<TimestampWithTimeZone> for TimestampMicrosecondArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<TimestampWithTimeZone> {
        if self.is_null(0) {
            None
        } else {
            Some(i64_to_timestamptz(self.value(0)))
        }
    }
}

// Timestamptz[]
impl ArrowArrayToPgType<Vec<Option<TimestampWithTimeZone>>> for TimestampMicrosecondArray {
    fn to_pg_type(
        self,
        _context: &ArrowToPgAttributeContext,
    ) -> Option<Vec<Option<TimestampWithTimeZone>>> {
        let mut vals = vec![];
        for val in self.iter() {
            let val = val.map(i64_to_timestamptz);
            vals.push(val);
        }
        Some(vals)
    }
}
