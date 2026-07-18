use arrow::array::{Array, Int16Array};

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Int2
impl ArrowArrayToPgType<i16> for Int16Array {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<i16> {
        if self.is_null(0) {
            None
        } else {
            let val = self.value(0);
            Some(val)
        }
    }
}

// Int2[]
impl ArrowArrayToPgType<Vec<Option<i16>>> for Int16Array {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<i16>>> {
        let mut vals = vec![];
        for val in self.iter() {
            vals.push(val);
        }
        Some(vals)
    }
}
