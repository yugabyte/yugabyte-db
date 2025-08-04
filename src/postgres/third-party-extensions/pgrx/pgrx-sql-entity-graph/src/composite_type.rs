/// Innards of a `composite_type!`
///
/// For SQL generation and further expansion.
/// Use this so you don't drop the span on the floor.
#[derive(Debug, Clone)]
pub struct CompositeTypeMacro {
    pub(crate) lifetime: Option<syn::Lifetime>,
    pub(crate) expr: syn::Expr,
    pub(crate) span: proc_macro2::Span,
}

impl syn::parse::Parse for CompositeTypeMacro {
    fn parse(input: syn::parse::ParseStream) -> Result<Self, syn::Error> {
        let span = input.span();
        let lifetime: Option<syn::Lifetime> = input.parse().ok();
        let _comma: Option<syn::Token![,]> = input.parse().ok();
        let expr = input.parse()?;
        Ok(Self { lifetime, expr, span })
    }
}

/// Take a `composite_type!` from a macro
pub fn handle_composite_type_macro(mac: &syn::Macro) -> syn::Result<CompositeTypeMacro> {
    let out: CompositeTypeMacro = mac.parse_body()?;
    Ok(out)
}

impl CompositeTypeMacro {
    /// Expands into the implementing type, explicitly eliding the lifetime
    /// if none is actually given.
    pub fn expand_with_lifetime(&self) -> syn::Type {
        let CompositeTypeMacro { lifetime, span, .. } = self.clone();
        let lifetime = lifetime.unwrap_or_else(|| syn::Lifetime::new("'_", span));
        syn::parse_quote! {
            ::pgrx::heap_tuple::PgHeapTuple<#lifetime, ::pgrx::pgbox::AllocatedByRust>
        }
    }
}
