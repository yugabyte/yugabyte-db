/// Evaluate an expression that resolves to any `impl ToTokens`, then produce a closure
/// for lazily combining errors only on the unhappy path of `syn::Result`
macro_rules! lazy_err {
    ($span_expr:expr, $lit:literal $(, $tokens:tt),*) => {
        {
            let spanning = $span_expr;
            || ::syn::Error::new_spanned(spanning, format!($lit, $($tokens)*))
        }
    }
}

pub fn with_array_brackets(s: String, brackets: bool) -> String {
    s + if brackets { "[]" } else { "" }
}

pub(crate) trait ErrHarder {
    fn more_error(self, closerr: impl FnOnce() -> syn::Error) -> Self;
}

impl<T> ErrHarder for syn::Result<T> {
    fn more_error(self, closerr: impl FnOnce() -> syn::Error) -> Self {
        self.map_err(|inner| {
            let mut e = inner.clone();
            e.combine(closerr());
            e
        })
    }
}

impl ErrHarder for syn::Error {
    fn more_error(mut self, closerr: impl FnOnce() -> syn::Error) -> Self {
        self.combine(closerr());
        self
    }
}
