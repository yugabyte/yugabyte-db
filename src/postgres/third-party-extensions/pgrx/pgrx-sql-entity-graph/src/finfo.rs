use proc_macro2::{Ident, TokenStream};
use quote::{format_ident, quote, quote_spanned};
use syn::{self, spanned::Spanned, ItemFn};

/// Generate the Postgres fn info record
///
/// Equivalent to PG_FUNCTION_INFO_V1, Postgres will sprintf the fn ident, then `dlsym(so, expected_name)`,
/// so it is important to pass exactly the ident that you want to have the record associated with!
pub fn finfo_v1_tokens(ident: proc_macro2::Ident) -> syn::Result<ItemFn> {
    let finfo_name = format_ident!("pg_finfo_{ident}");
    let tokens = quote! {
        #[no_mangle]
        #[doc(hidden)]
        pub extern "C" fn #finfo_name() -> &'static ::pgrx::pg_sys::Pg_finfo_record {
            const V1_API: ::pgrx::pg_sys::Pg_finfo_record = ::pgrx::pg_sys::Pg_finfo_record { api_version: 1 };
            &V1_API
        }
    };
    syn::parse2(tokens)
}

pub fn finfo_v1_extern_c(
    original: &syn::ItemFn,
    fcinfo: Ident,
    contents: TokenStream,
) -> syn::Result<ItemFn> {
    let original_name = &original.sig.ident;
    let wrapper_symbol = format_ident!("{}_wrapper", original_name);

    let synthetic = proc_macro2::Span::mixed_site();
    let synthetic = synthetic.located_at(original.sig.span());

    let tokens = quote_spanned! { synthetic =>
        #[no_mangle]
        #[doc(hidden)]
        pub unsafe extern "C-unwind" fn #wrapper_symbol(#fcinfo: ::pgrx::pg_sys::FunctionCallInfo) -> ::pgrx::pg_sys::Datum {
            #contents
        }
    };

    syn::parse2(tokens)
}
