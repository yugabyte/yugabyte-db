use arrow::array::{Array, TimestampMicrosecondArray};
use pgrx::datum::Timestamp;

use crate::type_compat::pg_arrow_type_conversions::i64_to_timestamp;

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Timestamp
impl ArrowArrayToPgType<Timestamp> for TimestampMicrosecondArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Timestamp> {
        if self.is_null(0) {
            None
        } else {
            Some(i64_to_timestamp(self.value(0)))
        }
    }
}

// Timestamp[]
impl ArrowArrayToPgType<Vec<Option<Timestamp>>> for TimestampMicrosecondArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<Timestamp>>> {
        let mut vals = vec![];
        for val in self.iter() {
            let val = val.map(i64_to_timestamp);
            vals.push(val);
        }
        Some(vals)
    }
}
