use arrow::array::{Array, BinaryArray};

use crate::type_compat::geometry::Geometry;

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Geometry
impl ArrowArrayToPgType<Geometry> for BinaryArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Geometry> {
        if self.is_null(0) {
            None
        } else {
            Some(self.value(0).to_vec().into())
        }
    }
}

// Geometry[]
impl ArrowArrayToPgType<Vec<Option<Geometry>>> for BinaryArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<Geometry>>> {
        let mut vals = vec![];
        for val in self.iter() {
            if let Some(val) = val {
                vals.push(Some(val.to_vec().into()));
            } else {
                vals.push(None);
            }
        }

        Some(vals)
    }
}
