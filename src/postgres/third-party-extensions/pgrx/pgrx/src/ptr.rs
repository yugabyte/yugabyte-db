pub(crate) trait PointerExt {
    fn is_non_null(&self) -> bool;
}

impl<T> PointerExt for *mut T {
    fn is_non_null(&self) -> bool {
        !self.is_null()
    }
}

impl<T> PointerExt for *const T {
    fn is_non_null(&self) -> bool {
        !self.is_null()
    }
}
