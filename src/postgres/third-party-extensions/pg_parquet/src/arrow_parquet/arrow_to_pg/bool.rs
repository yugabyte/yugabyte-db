use arrow::array::{Array, BooleanArray};

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Bool
impl ArrowArrayToPgType<bool> for BooleanArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<bool> {
        if self.is_null(0) {
            None
        } else {
            let val = self.value(0);
            Some(val)
        }
    }
}

// Bool[]
impl ArrowArrayToPgType<Vec<Option<bool>>> for BooleanArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<bool>>> {
        let mut vals = vec![];
        for val in self.iter() {
            vals.push(val);
        }

        Some(vals)
    }
}
