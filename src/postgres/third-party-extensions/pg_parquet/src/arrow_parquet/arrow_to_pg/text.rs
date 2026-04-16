use arrow::array::{Array, StringArray};

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Text
impl ArrowArrayToPgType<String> for StringArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<String> {
        if self.is_null(0) {
            None
        } else {
            let val = self.value(0);
            Some(val.to_string())
        }
    }
}

// Text[]
impl ArrowArrayToPgType<Vec<Option<String>>> for StringArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<String>>> {
        let mut vals = vec![];
        for val in self.iter() {
            let val = val.map(|val| val.to_string());
            vals.push(val);
        }
        Some(vals)
    }
}
