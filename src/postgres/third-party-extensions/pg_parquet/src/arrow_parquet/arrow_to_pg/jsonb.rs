use arrow::array::{Array, StringArray};
use pgrx::JsonB;

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// JsonB
impl ArrowArrayToPgType<JsonB> for StringArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<JsonB> {
        if self.is_null(0) {
            None
        } else {
            let val = self.value(0);
            Some(JsonB(
                serde_json::from_str(val).unwrap_or_else(|_| panic!("invalid jsonb: {}", val)),
            ))
        }
    }
}

// JsonB[]
impl ArrowArrayToPgType<Vec<Option<JsonB>>> for StringArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<Vec<Option<JsonB>>> {
        let mut vals = vec![];
        for val in self.iter() {
            let val = val.map(|val| {
                JsonB(
                    serde_json::from_str(val).unwrap_or_else(|_| panic!("invalid jsonb: {}", val)),
                )
            });
            vals.push(val);
        }
        Some(vals)
    }
}
