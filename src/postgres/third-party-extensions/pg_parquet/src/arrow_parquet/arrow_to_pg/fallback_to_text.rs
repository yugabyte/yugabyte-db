use arrow::array::{Array, StringArray};

use crate::type_compat::fallback_to_text::FallbackToText;

use super::{ArrowArrayToPgType, ArrowToPgAttributeContext};

// Text representation of any type
impl ArrowArrayToPgType<FallbackToText> for StringArray {
    fn to_pg_type(self, _context: &ArrowToPgAttributeContext) -> Option<FallbackToText> {
        if self.is_null(0) {
            None
        } else {
            let text_repr = self.value(0).to_string();
            let val = FallbackToText(text_repr);
            Some(val)
        }
    }
}

// Text[] representation of any type
impl ArrowArrayToPgType<Vec<Option<FallbackToText>>> for StringArray {
    fn to_pg_type(
        self,
        _context: &ArrowToPgAttributeContext,
    ) -> Option<Vec<Option<FallbackToText>>> {
        let mut vals = vec![];
        for val in self.iter() {
            let val = val.map(|val| FallbackToText(val.to_string()));
            vals.push(val);
        }
        Some(vals)
    }
}
