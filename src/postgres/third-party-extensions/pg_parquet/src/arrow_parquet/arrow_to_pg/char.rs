use arrow::array::{Array, StringArray};

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Char
impl ArrowArrayToPgType<i8> for StringArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<i8> {
        if self.is_null(0) {
            None
        } else {
            let val = self.value(0);
            let val: i8 = val.chars().next().expect("unexpected ascii char") as i8;
            Some(val)
        }
    }
}

// Char[]
impl ArrowArrayToPgType<Vec<Option<i8>>> for StringArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<i8>>> {
        let mut vals = vec![];
        for val in self.iter() {
            let val = val.map(|val| {
                let val: i8 = val.chars().next().expect("unexpected ascii char") as i8;
                val
            });
            vals.push(val);
        }
        Some(vals)
    }
}
