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
    receive_fn: Option<Ident>,
    send_fn: Option<Ident>,
    to_sql_config: ToSqlConfig,
    alignment: Alignment,
}

impl PostgresTypeDerive {
    pub fn new(
        name: Ident,
        generics: Generics,
        in_fn: Ident,
        out_fn: Ident,
        receive_fn: Option<Ident>,
        send_fn: Option<Ident>,
        to_sql_config: ToSqlConfig,
        alignment: Alignment,
    ) -> Result<CodeEnrichment<Self>, syn::Error> {
        if !to_sql_config.overrides_default() {
            crate::ident_is_acceptable_to_postgres(&name)?;
        }
        Ok(CodeEnrichment(Self {
            generics,
            name,
            in_fn,
            out_fn,
            receive_fn,
            send_fn,
            to_sql_config,
            alignment,
        }))
    }

    pub fn from_derive_input(
        derive_input: DeriveInput,
        pg_binary_protocol: bool,
    ) -> Result<CodeEnrichment<Self>, syn::Error> {
        match derive_input.data {
            syn::Data::Struct(_) | syn::Data::Enum(_) => {}
            syn::Data::Union(_) => {
                return Err(syn::Error::new(derive_input.ident.span(), "expected struct or enum"));
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
        let funcname_receive = (pg_binary_protocol).then(|| {
            Ident::new(
                &format!("{}_recv", derive_input.ident).to_lowercase(),
                derive_input.ident.span(),
            )
        });
        let funcname_send = (pg_binary_protocol).then(|| {
            Ident::new(
                &format!("{}_send", derive_input.ident).to_lowercase(),
                derive_input.ident.span(),
            )
        });
        let alignment = Alignment::from_attributes(derive_input.attrs.as_slice())?;
        Self::new(
            derive_input.ident,
            derive_input.generics,
            funcname_in,
            funcname_out,
            funcname_receive,
            funcname_send,
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
        let sql_graph_entity_fn_name = format_ident!("__pgrx_schema_type_{}", self.name);

        let to_sql_config = &self.to_sql_config;
        let to_sql_config_len = to_sql_config.section_len_tokens();
        let receive_fn_len = self
            .receive_fn
            .as_ref()
            .map(|f| {
                quote! {
                    ::pgrx::pgrx_sql_entity_graph::section::bool_len()
                        + ::pgrx::pgrx_sql_entity_graph::section::str_len(stringify!(#f))
                }
            })
            .unwrap_or_else(|| quote! { ::pgrx::pgrx_sql_entity_graph::section::bool_len() });
        let receive_fn_writer = self
            .receive_fn
            .as_ref()
            .map(|f| quote! { .bool(true).str(stringify!(#f)) })
            .unwrap_or_else(|| quote! { .bool(false) });
        let send_fn_len = self
            .send_fn
            .as_ref()
            .map(|f| {
                quote! {
                    ::pgrx::pgrx_sql_entity_graph::section::bool_len()
                        + ::pgrx::pgrx_sql_entity_graph::section::str_len(stringify!(#f))
                }
            })
            .unwrap_or_else(|| quote! { ::pgrx::pgrx_sql_entity_graph::section::bool_len() });
        let send_fn_writer = self
            .send_fn
            .as_ref()
            .map(|f| quote! { .bool(true).str(stringify!(#f)) })
            .unwrap_or_else(|| quote! { .bool(false) });

        let alignment_len = match &self.alignment {
            Alignment::On => quote! {
                ::pgrx::pgrx_sql_entity_graph::section::bool_len()
                    + ::pgrx::pgrx_sql_entity_graph::section::u32_len()
            },
            Alignment::Off => quote! { ::pgrx::pgrx_sql_entity_graph::section::bool_len() },
        };
        let alignment_writer = match &self.alignment {
            Alignment::On => quote! { .bool(true).u32(::std::mem::align_of::<#name>() as u32) },
            Alignment::Off => quote! { .bool(false) },
        };
        let type_ident = quote! { ::pgrx::pgrx_resolved_type!(#name #anon_ty_gen) };
        let payload_len = quote! {
            ::pgrx::pgrx_sql_entity_graph::section::u8_len()
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(stringify!(#name))
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(file!())
                + ::pgrx::pgrx_sql_entity_graph::section::u32_len()
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(module_path!())
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(stringify!(#name #anon_ty_gen))
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(#type_ident)
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(stringify!(#in_fn))
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(stringify!(#out_fn))
                + (#receive_fn_len)
                + (#send_fn_len)
                + (#to_sql_config_len)
                + (#alignment_len)
        };
        let total_len = quote! {
            ::pgrx::pgrx_sql_entity_graph::section::u32_len() + (#payload_len)
        };
        let writer = to_sql_config.section_writer_tokens(quote! {
            ::pgrx::pgrx_sql_entity_graph::section::EntryWriter::<{ #total_len }>::new()
                .u32((#payload_len) as u32)
                .u8(::pgrx::pgrx_sql_entity_graph::section::ENTITY_TYPE)
                .str(stringify!(#name))
                .str(file!())
                .u32(line!())
                .str(module_path!())
                .str(stringify!(#name #anon_ty_gen))
                .str(#type_ident)
                .str(stringify!(#in_fn))
                .str(stringify!(#out_fn))
                #receive_fn_writer
                #send_fn_writer
        });

        quote! {
            unsafe impl #impl_generics ::pgrx::pgrx_sql_entity_graph::metadata::SqlTranslatable for #name #ty_generics #where_clauses {
                const TYPE_IDENT: &'static str = #type_ident;
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
                #writer
                    #alignment_writer
                    .finish()
            );
        }
    }
}

impl ToRustCodeTokens for PostgresTypeDerive {}

impl Parse for CodeEnrichment<PostgresTypeDerive> {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        let ItemStruct { attrs, ident, generics, .. } = input.parse()?;

        let pg_binary_protocol = attrs.iter().any(|a| a.path().is_ident("pg_binary_protocol"));

        let to_sql_config = ToSqlConfig::from_attributes(attrs.as_slice())?.unwrap_or_default();
        let in_fn = Ident::new(&format!("{ident}_in").to_lowercase(), ident.span());
        let out_fn = Ident::new(&format!("{ident}_out").to_lowercase(), ident.span());
        let receive_fn = (pg_binary_protocol)
            .then(|| Ident::new(&format!("{ident}_recv").to_lowercase(), ident.span()));
        let send_fn = (pg_binary_protocol)
            .then(|| Ident::new(&format!("{ident}_send").to_lowercase(), ident.span()));
        let alignment = Alignment::from_attributes(attrs.as_slice())?;
        PostgresTypeDerive::new(
            ident,
            generics,
            in_fn,
            out_fn,
            receive_fn,
            send_fn,
            to_sql_config,
            alignment,
        )
    }
}
