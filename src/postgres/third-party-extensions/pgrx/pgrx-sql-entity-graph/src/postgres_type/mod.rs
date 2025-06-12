//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
/*!

`#[derive(PostgresType)]` related macro expansion for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
pub mod entity;

use crate::enrich::{ToEntityGraphTokens, ToRustCodeTokens};
use proc_macro2::{Ident, TokenStream as TokenStream2};
use quote::{format_ident, quote};
use syn::parse::{Parse, ParseStream};
use syn::{DeriveInput, Generics, ItemStruct, Lifetime, LifetimeParam};

pub use crate::postgres_type::entity::Alignment;
use crate::{CodeEnrichment, ToSqlConfig};

/// A parsed `#[derive(PostgresType)]` item.
///
/// It should be used with [`syn::parse::Parse`] functions.
///
/// Using [`quote::ToTokens`] will output the declaration for a [`PostgresTypeEntity`][crate::PostgresTypeEntity].
///
/// ```rust
/// use syn::{Macro, parse::Parse, parse_quote, parse};
/// use quote::{quote, ToTokens};
/// use pgrx_sql_entity_graph::PostgresTypeDerive;
///
/// # fn main() -> eyre::Result<()> {
/// use pgrx_sql_entity_graph::CodeEnrichment;
/// let parsed: CodeEnrichment<PostgresTypeDerive> = parse_quote! {
///     #[derive(PostgresType)]
///     struct Example<'a> {
///         demo: &'a str,
///     }
/// };
/// let sql_graph_entity_tokens = parsed.to_token_stream();
/// # Ok(())
/// # }
/// ```
#[derive(Debug, Clone)]
pub struct PostgresTypeDerive {
    name: Ident,
    generics: Generics,
    in_fn: Ident,
    out_fn: Ident,
    to_sql_config: ToSqlConfig,
    alignment: Alignment,
}

impl PostgresTypeDerive {
    pub fn new(
        name: Ident,
        generics: Generics,
        in_fn: Ident,
        out_fn: Ident,
        to_sql_config: ToSqlConfig,
        alignment: Alignment,
    ) -> Result<CodeEnrichment<Self>, syn::Error> {
        if !to_sql_config.overrides_default() {
            crate::ident_is_acceptable_to_postgres(&name)?;
        }
        Ok(CodeEnrichment(Self { generics, name, in_fn, out_fn, to_sql_config, alignment }))
    }

    pub fn from_derive_input(
        derive_input: DeriveInput,
    ) -> Result<CodeEnrichment<Self>, syn::Error> {
        match derive_input.data {
            syn::Data::Struct(_) | syn::Data::Enum(_) => {}
            syn::Data::Union(_) => {
                return Err(syn::Error::new(derive_input.ident.span(), "expected struct or enum"))
            }
        };
        let to_sql_config =
            ToSqlConfig::from_attributes(derive_input.attrs.as_slice())?.unwrap_or_default();
        let funcname_in = Ident::new(
            &format!("{}_in", derive_input.ident).to_lowercase(),
            derive_input.ident.span(),
        );
        let funcname_out = Ident::new(
            &format!("{}_out", derive_input.ident).to_lowercase(),
            derive_input.ident.span(),
        );
        let alignment = Alignment::from_attributes(derive_input.attrs.as_slice())?;
        Self::new(
            derive_input.ident,
            derive_input.generics,
            funcname_in,
            funcname_out,
            to_sql_config,
            alignment,
        )
    }
}

impl ToEntityGraphTokens for PostgresTypeDerive {
    fn to_entity_graph_tokens(&self) -> TokenStream2 {
        let name = &self.name;
        let generics = self.generics.clone();
        let (impl_generics, ty_generics, where_clauses) = generics.split_for_impl();

        // We need some generics we can use inside a fn without a lifetime for qualified paths.
        let mut anon_generics = generics.clone();
        anon_generics.params = anon_generics
            .params
            .into_iter()
            .flat_map(|param| match param {
                item @ syn::GenericParam::Type(_) | item @ syn::GenericParam::Const(_) => {
                    Some(item)
                }
                syn::GenericParam::Lifetime(lt_def) => Some(syn::GenericParam::Lifetime(
                    LifetimeParam::new(Lifetime::new("'_", lt_def.lifetime.span())),
                )),
            })
            .collect();
        let (_, anon_ty_gen, _) = anon_generics.split_for_impl();

        let in_fn = &self.in_fn;
        let out_fn = &self.out_fn;

        let sql_graph_entity_fn_name = format_ident!("__pgrx_internals_type_{}", self.name);

        let to_sql_config = &self.to_sql_config;

        let alignment = match &self.alignment {
            Alignment::On => quote! { Some(::std::mem::align_of::<#name>()) },
            Alignment::Off => quote! { None },
        };

        quote! {
            unsafe impl #impl_generics ::pgrx::pgrx_sql_entity_graph::metadata::SqlTranslatable for #name #ty_generics #where_clauses {
                fn argument_sql() -> core::result::Result<::pgrx::pgrx_sql_entity_graph::metadata::SqlMapping, ::pgrx::pgrx_sql_entity_graph::metadata::ArgumentError> {
                    Ok(::pgrx::pgrx_sql_entity_graph::metadata::SqlMapping::As(String::from(stringify!(#name))))
                }

                fn return_sql() -> core::result::Result<::pgrx::pgrx_sql_entity_graph::metadata::Returns, ::pgrx::pgrx_sql_entity_graph::metadata::ReturnsError> {
                    Ok(::pgrx::pgrx_sql_entity_graph::metadata::Returns::One(::pgrx::pgrx_sql_entity_graph::metadata::SqlMapping::As(String::from(stringify!(#name)))))
                }
            }


            #[no_mangle]
            #[doc(hidden)]
            #[allow(nonstandard_style, unknown_lints, clippy::no_mangle_with_rust_abi)]
            pub extern "Rust" fn  #sql_graph_entity_fn_name() -> ::pgrx::pgrx_sql_entity_graph::SqlGraphEntity {
                extern crate alloc;
                use alloc::vec::Vec;
                use alloc::vec;
                use alloc::string::{String, ToString};
                use ::pgrx::datum::WithTypeIds;

                let mut mappings = Default::default();
                <#name #anon_ty_gen as ::pgrx::datum::WithTypeIds>::register_with_refs(
                    &mut mappings,
                    stringify!(#name).to_string()
                );
                ::pgrx::datum::WithSizedTypeIds::<#name #anon_ty_gen>::register_sized_with_refs(
                    &mut mappings,
                    stringify!(#name).to_string()
                );
                ::pgrx::datum::WithArrayTypeIds::<#name #anon_ty_gen>::register_array_with_refs(
                    &mut mappings,
                    stringify!(#name).to_string()
                );
                ::pgrx::datum::WithVarlenaTypeIds::<#name #anon_ty_gen>::register_varlena_with_refs(
                    &mut mappings,
                    stringify!(#name).to_string()
                );
                let submission = ::pgrx::pgrx_sql_entity_graph::PostgresTypeEntity {
                    name: stringify!(#name),
                    file: file!(),
                    line: line!(),
                    module_path: module_path!(),
                    full_path: core::any::type_name::<#name #anon_ty_gen>(),
                    mappings: mappings.into_iter().collect(),
                    in_fn: stringify!(#in_fn),
                    in_fn_module_path: {
                        let in_fn = stringify!(#in_fn);
                        let mut path_items: Vec<_> = in_fn.split("::").collect();
                        let _ = path_items.pop(); // Drop the one we don't want.
                        path_items.join("::")
                    },
                    out_fn: stringify!(#out_fn),
                    out_fn_module_path: {
                        let out_fn = stringify!(#out_fn);
                        let mut path_items: Vec<_> = out_fn.split("::").collect();
                        let _ = path_items.pop(); // Drop the one we don't want.
                        path_items.join("::")
                    },
                    to_sql_config: #to_sql_config,
                    alignment: #alignment,
                };
                ::pgrx::pgrx_sql_entity_graph::SqlGraphEntity::Type(submission)
            }
        }
    }
}

impl ToRustCodeTokens for PostgresTypeDerive {}

impl Parse for CodeEnrichment<PostgresTypeDerive> {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        let ItemStruct { attrs, ident, generics, .. } = input.parse()?;
        let to_sql_config = ToSqlConfig::from_attributes(attrs.as_slice())?.unwrap_or_default();
        let in_fn = Ident::new(&format!("{}_in", ident).to_lowercase(), ident.span());
        let out_fn = Ident::new(&format!("{}_out", ident).to_lowercase(), ident.span());
        let alignment = Alignment::from_attributes(attrs.as_slice())?;
        PostgresTypeDerive::new(ident, generics, in_fn, out_fn, to_sql_config, alignment)
    }
}
