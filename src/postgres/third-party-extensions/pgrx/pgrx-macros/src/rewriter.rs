//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
extern crate proc_macro;

use quote::{quote, quote_spanned};
use std::mem;
use std::ops::Deref;
use std::str::FromStr;
use syn::punctuated::Punctuated;
use syn::spanned::Spanned;
use syn::{
    Expr, ExprLit, FnArg, ForeignItem, ForeignItemFn, ForeignItemStatic, GenericParam, ItemFn,
    ItemForeignMod, Lit, Pat, Signature, Token, Visibility,
};

macro_rules! format_ident {
    ($s:literal, $e:expr) => {{
        let mut synthetic = $e.clone();
        synthetic.set_span(proc_macro2::Span::call_site().located_at($e.span()));
        quote::format_ident!($s, synthetic)
    }};
}

pub fn extern_block(block: ItemForeignMod) -> proc_macro2::TokenStream {
    let mut stream = proc_macro2::TokenStream::new();

    for item in block.items.into_iter() {
        stream.extend(foreign_item(item, &block.abi));
    }

    stream
}

pub fn item_fn_without_rewrite(mut func: ItemFn) -> syn::Result<proc_macro2::TokenStream> {
    // remember the original visibility and signature classifications as we want
    // to use those for the outer function
    let input_func_name = func.sig.ident.to_string();
    let sig = func.sig.clone();
    let vis = func.vis.clone();
    let attrs = mem::take(&mut func.attrs);
    let generics = func.sig.generics.clone();

    if sig.abi.clone().and_then(|abi| abi.name).is_none_or(|name| name.value() != "C-unwind") {
        panic!("#[pg_guard] must be combined with extern \"C-unwind\"");
    }

    if attrs.iter().any(|attr| attr.path().is_ident("no_mangle"))
        && generics.params.iter().any(|p| match p {
            GenericParam::Type(_) => true,
            GenericParam::Lifetime(_) => false,
            GenericParam::Const(_) => true,
        })
    {
        panic!("#[pg_guard] for function with generic parameters must not be combined with #[no_mangle]");
    }

    // but for the inner function (the one we're wrapping) we don't need any kind of
    // abi classification
    func.sig.abi = None;

    // nor do we need a visibility beyond "private"
    func.vis = Visibility::Inherited;

    func.sig.ident = format_ident!("{}_inner", func.sig.ident);

    let arg_list = build_arg_list(&sig, false)?;
    let func_name = func.sig.ident.clone();
    let func_name = format_ident!("{}", func_name);

    let prolog = if input_func_name == "__pgrx_private_shmem_hook"
        || input_func_name == "__pgrx_private_shmem_request_hook"
    {
        // we do not want "no_mangle" on these functions
        quote! {}
    } else if input_func_name == "_PG_init" || input_func_name == "_PG_fini" {
        quote! {
            #[allow(non_snake_case)]
            #[no_mangle]
        }
    } else {
        quote! {}
    };

    let body = if generics.params.is_empty() {
        quote! { #func_name(#arg_list) }
    } else {
        let ty = generics
            .params
            .into_iter()
            .filter_map(|p| match p {
                GenericParam::Type(ty) => Some(ty.ident),
                GenericParam::Const(c) => Some(c.ident),
                GenericParam::Lifetime(_) => None,
            })
            .collect::<Punctuated<_, Token![,]>>();
        quote! { #func_name::<#ty>(#arg_list) }
    };

    let synthetic = proc_macro2::Span::mixed_site().located_at(func.span());

    Ok(quote_spanned! {synthetic=>
        #prolog
        #(#attrs)*
        #vis #sig {
            #[allow(non_snake_case)]
            #func

            #[allow(unused_unsafe)]
            unsafe {
                // NB: this is purposely not spelled `::pgrx` as pgrx itself uses #[pg_guard]
                pgrx::pg_sys::submodules::panic::pgrx_extern_c_guard(move || #body )
            }
        }
    })
}

fn foreign_item(item: ForeignItem, abi: &syn::Abi) -> syn::Result<proc_macro2::TokenStream> {
    match item {
        ForeignItem::Fn(func) => {
            if func.sig.variadic.is_some() {
                return Ok(quote! { #abi { #func } });
            }

            foreign_item_fn(&func, abi)
        }
        ForeignItem::Static(variable) => foreign_item_static(&variable, abi),
        _ => Ok(quote! { #abi { #item } }),
    }
}

fn foreign_item_fn(func: &ForeignItemFn, abi: &syn::Abi) -> syn::Result<proc_macro2::TokenStream> {
    let func_name = func.sig.ident.clone();
    let arg_list = rename_arg_list(&func.sig)?;
    let arg_list_with_types = rename_arg_list_with_types(&func.sig)?;
    let return_type = func.sig.output.clone();
    let link_with_cshim = func.attrs.iter().any(|attr| match &attr.meta {
        syn::Meta::NameValue(kv) if kv.path.get_ident().filter(|x| *x == "link_name").is_some() => {
            if let Expr::Lit(ExprLit { lit: Lit::Str(value), .. }) = &kv.value {
                value.value().ends_with("__pgrx_cshim")
            } else {
                false
            }
        }
        _ => false,
    });
    let link = if link_with_cshim {
        quote! {}
    } else {
        quote! { #[cfg_attr(target_os = "windows", link(name = "postgres"))] }
    };

    Ok(quote! {
        #[inline]
        #[track_caller]
        pub unsafe fn #func_name ( #arg_list_with_types ) #return_type {
            crate::ffi::pg_guard_ffi_boundary(move || {
                #link #abi { #func }
                #func_name(#arg_list)
            })
        }
    })
}

fn foreign_item_static(
    variable: &ForeignItemStatic,
    abi: &syn::Abi,
) -> syn::Result<proc_macro2::TokenStream> {
    let link = quote! { #[cfg_attr(target_os = "windows", link(name = "postgres"))] };
    Ok(quote! {
        #link #abi { #variable }
    })
}

#[allow(clippy::cmp_owned)]
fn build_arg_list(sig: &Signature, suffix_arg_name: bool) -> syn::Result<proc_macro2::TokenStream> {
    let mut arg_list = proc_macro2::TokenStream::new();

    for arg in &sig.inputs {
        match arg {
            FnArg::Typed(ty) => {
                if let Pat::Ident(ident) = ty.pat.deref() {
                    if suffix_arg_name && ident.ident.to_string() != "fcinfo" {
                        let ident = format_ident!("{}_", ident.ident);
                        arg_list.extend(quote! { #ident, });
                    } else {
                        let ident = format_ident!("{}", ident.ident);
                        arg_list.extend(quote! { #ident, });
                    }
                } else {
                    return Err(syn::Error::new(
                        ty.pat.span(),
                        "Unknown argument pattern in `#[pg_guard]` function",
                    ));
                }
            }
            a @ FnArg::Receiver(_) => {
                return Err(syn::Error::new(
                    a.span(),
                    "#[pg_guard] doesn't support external functions with 'self' as the argument",
                ))
            }
        }
    }

    Ok(arg_list)
}

fn rename_arg_list(sig: &Signature) -> syn::Result<proc_macro2::TokenStream> {
    let mut arg_list = proc_macro2::TokenStream::new();

    for arg in &sig.inputs {
        match arg {
            FnArg::Typed(ty) => {
                if let Pat::Ident(ident) = ty.pat.deref() {
                    // prefix argument name with "arg_""
                    let name = format_ident!("arg_{}", ident.ident);
                    arg_list.extend(quote! { #name, });
                } else {
                    return Err(syn::Error::new(
                        ty.pat.span(),
                        "Unknown argument pattern in `#[pg_guard]` function",
                    ));
                }
            }
            a @ FnArg::Receiver(_) => {
                return Err(syn::Error::new(
                    a.span(),
                    "#[pg_guard] doesn't support external functions with 'self' as the argument",
                ))
            }
        }
    }

    Ok(arg_list)
}

fn rename_arg_list_with_types(sig: &Signature) -> syn::Result<proc_macro2::TokenStream> {
    let mut arg_list = proc_macro2::TokenStream::new();

    for arg in &sig.inputs {
        match arg {
            FnArg::Typed(ty) => {
                if let Pat::Ident(_) = ty.pat.deref() {
                    // prefix argument name with a "arg_"
                    let arg = proc_macro2::TokenStream::from_str(&format!("arg_{}", quote! {#ty}))
                        .unwrap();
                    arg_list.extend(quote! { #arg, });
                } else {
                    return Err(syn::Error::new(
                        ty.pat.span(),
                        "Unknown argument pattern in `#[pg_guard]` function",
                    ));
                }
            }
            a @ FnArg::Receiver(_) => {
                return Err(syn::Error::new(
                    a.span(),
                    "#[pg_guard] doesn't support external functions with 'self' as the argument",
                ))
            }
        }
    }

    Ok(arg_list)
}
