use arrow::array::{Array, BinaryArray};

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Bytea
impl ArrowArrayToPgType<Vec<u8>> for BinaryArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<u8>> {
        if self.is_null(0) {
            None
        } else {
            Some(self.value(0).to_vec())
        }
    }
}

// Bytea[]
impl ArrowArrayToPgType<Vec<Option<Vec<u8>>>> for BinaryArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<Vec<u8>>>> {
        let mut vals = vec![];
        for val in self.iter() {
            if let Some(val) = val {
                vals.push(Some(val.to_vec()));
            } else {
                vals.push(None);
            }
        }

        Some(vals)
    }
}
