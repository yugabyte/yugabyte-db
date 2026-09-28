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

`#[derive(PostgresEnum)]` related macro expansion for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
pub mod entity;

use crate::enrich::{ToEntityGraphTokens, ToRustCodeTokens};
use crate::{CodeEnrichment, ToSqlConfig};
use proc_macro2::{Span, TokenStream as TokenStream2};
use quote::{format_ident, quote};
use syn::parse::{Parse, ParseStream};
use syn::punctuated::Punctuated;
use syn::{DeriveInput, Generics, Ident, ItemEnum, Token};

/// A parsed `#[derive(PostgresEnum)]` item.
///
/// It should be used with [`syn::parse::Parse`] functions.
///
/// Using [`quote::ToTokens`] will output the declaration for a `pgrx::datum::pgrx_sql_entity_graph::PostgresEnumEntity`.
///
/// ```rust
/// use syn::{Macro, parse::Parse, parse_quote, parse};
/// use quote::{quote, ToTokens};
/// use pgrx_sql_entity_graph::PostgresEnum;
///
/// # fn main() -> eyre::Result<()> {
/// use pgrx_sql_entity_graph::CodeEnrichment;
/// let parsed: CodeEnrichment<PostgresEnum> = parse_quote! {
///     #[derive(PostgresEnum)]
///     enum Demo {
///         Example,
///     }
/// };
/// let sql_graph_entity_tokens = parsed.to_token_stream();
/// # Ok(())
/// # }
/// ```
#[derive(Debug, Clone)]
pub struct PostgresEnum {
    name: Ident,
    generics: Generics,
    variants: Punctuated<syn::Variant, Token![,]>,
    to_sql_config: ToSqlConfig,
}

impl PostgresEnum {
    pub fn new(
        name: Ident,
        generics: Generics,
        variants: Punctuated<syn::Variant, Token![,]>,
        to_sql_config: ToSqlConfig,
    ) -> Result<CodeEnrichment<Self>, syn::Error> {
        if !to_sql_config.overrides_default() {
            crate::ident_is_acceptable_to_postgres(&name)?;
        }

        Ok(CodeEnrichment(Self { name, generics, variants, to_sql_config }))
    }

    pub fn from_derive_input(
        derive_input: DeriveInput,
    ) -> Result<CodeEnrichment<Self>, syn::Error> {
        let to_sql_config =
            ToSqlConfig::from_attributes(derive_input.attrs.as_slice())?.unwrap_or_default();
        let data_enum = match derive_input.data {
            syn::Data::Enum(data_enum) => data_enum,
            syn::Data::Union(_) | syn::Data::Struct(_) => {
                return Err(syn::Error::new(derive_input.ident.span(), "expected enum"));
            }
        };
        Self::new(derive_input.ident, derive_input.generics, data_enum.variants, to_sql_config)
    }
}

impl ToEntityGraphTokens for PostgresEnum {
    fn to_entity_graph_tokens(&self) -> TokenStream2 {
        // It's important we remap all lifetimes we spot to `'static` so they can be used during inventory submission.
        let name = self.name.clone();
        let mut static_generics = self.generics.clone();
        static_generics.params = static_generics
            .params
            .clone()
            .into_iter()
            .flat_map(|param| match param {
                item @ syn::GenericParam::Type(_) | item @ syn::GenericParam::Const(_) => {
                    Some(item)
                }
                syn::GenericParam::Lifetime(mut lifetime) => {
                    lifetime.lifetime.ident = Ident::new("static", Span::call_site());
                    Some(syn::GenericParam::Lifetime(lifetime))
                }
            })
            .collect();
        let mut staticless_generics = self.generics.clone();
        staticless_generics.params = static_generics
            .params
            .clone()
            .into_iter()
            .flat_map(|param| match param {
                item @ syn::GenericParam::Type(_) | item @ syn::GenericParam::Const(_) => {
                    Some(item)
                }
                syn::GenericParam::Lifetime(_) => None,
            })
            .collect();
        let (staticless_impl_generics, _staticless_ty_generics, _staticless_where_clauses) =
            staticless_generics.split_for_impl();
        let (_static_impl_generics, static_ty_generics, static_where_clauses) =
            static_generics.split_for_impl();

        let variants =
            self.variants.iter().map(|variant| variant.ident.clone()).collect::<Vec<_>>();
        let sql_graph_entity_fn_name = format_ident!("__pgrx_schema_enum_{}", name);

        let to_sql_config = &self.to_sql_config;
        let to_sql_config_len = to_sql_config.section_len_tokens();
        let variants_len = quote! {
            ::pgrx::pgrx_sql_entity_graph::section::list_len(&[
                #( ::pgrx::pgrx_sql_entity_graph::section::str_len(stringify!(#variants)) ),*
            ])
        };
        let payload_len = quote! {
            ::pgrx::pgrx_sql_entity_graph::section::u8_len()
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(stringify!(#name))
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(file!())
                + ::pgrx::pgrx_sql_entity_graph::section::u32_len()
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(module_path!())
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(stringify!(#name #static_ty_generics))
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(<#name #static_ty_generics as ::pgrx::pgrx_sql_entity_graph::metadata::SqlTranslatable>::TYPE_IDENT)
                + (#variants_len)
                + (#to_sql_config_len)
        };
        let total_len = quote! {
            ::pgrx::pgrx_sql_entity_graph::section::u32_len() + (#payload_len)
        };
        let writer = to_sql_config.section_writer_tokens(quote! {
            ::pgrx::pgrx_sql_entity_graph::section::EntryWriter::<{ #total_len }>::new()
                .u32((#payload_len) as u32)
                .u8(::pgrx::pgrx_sql_entity_graph::section::ENTITY_ENUM)
                .str(stringify!(#name))
                .str(file!())
                .u32(line!())
                .str(module_path!())
                .str(stringify!(#name #static_ty_generics))
                .str(<#name #static_ty_generics as ::pgrx::pgrx_sql_entity_graph::metadata::SqlTranslatable>::TYPE_IDENT)
                .u32([ #( stringify!(#variants) ),* ].len() as u32)
                #( .str(stringify!(#variants)) )*
        });

        quote! {
            unsafe impl #staticless_impl_generics ::pgrx::pgrx_sql_entity_graph::metadata::SqlTranslatable for #name #static_ty_generics #static_where_clauses {
                const TYPE_IDENT: &'static str = ::pgrx::pgrx_resolved_type!(#name #static_ty_generics);
                const TYPE_ORIGIN: ::pgrx::pgrx_sql_entity_graph::metadata::TypeOrigin =
                    ::pgrx::pgrx_sql_entity_graph::metadata::TypeOrigin::ThisExtension;
                const ARGUMENT_SQL: core::result::Result<
                    ::pgrx::pgrx_sql_entity_graph::metadata::SqlMappingRef,
                    ::pgrx::pgrx_sql_entity_graph::metadata::ArgumentError,
                > = Ok(::pgrx::pgrx_sql_entity_graph::metadata::SqlMappingRef::As(stringify!(#name)));
                const RETURN_SQL: core::result::Result<
                    ::pgrx::pgrx_sql_entity_graph::metadata::ReturnsRef,
                    ::pgrx::pgrx_sql_entity_graph::metadata::ReturnsError,
                > = Ok(::pgrx::pgrx_sql_entity_graph::metadata::ReturnsRef::One(
                    ::pgrx::pgrx_sql_entity_graph::metadata::SqlMappingRef::As(stringify!(#name))
                ));
            }

            ::pgrx::pgrx_sql_entity_graph::__pgrx_schema_entry!(
                #sql_graph_entity_fn_name,
                #total_len,
                #writer.finish()
            );
        }
    }
}

impl ToRustCodeTokens for PostgresEnum {}

impl Parse for CodeEnrichment<PostgresEnum> {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        let parsed: ItemEnum = input.parse()?;
        let to_sql_config =
            ToSqlConfig::from_attributes(parsed.attrs.as_slice())?.unwrap_or_default();
        PostgresEnum::new(parsed.ident, parsed.generics, parsed.variants, to_sql_config)
    }
}
