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

`#[derive(PostgresHash)]` related macro expansion for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
pub mod entity;

use crate::enrich::{ToEntityGraphTokens, ToRustCodeTokens};
use proc_macro2::TokenStream as TokenStream2;
use quote::{format_ident, quote};
use syn::parse::{Parse, ParseStream};
use syn::{DeriveInput, Ident};

use crate::{CodeEnrichment, ToSqlConfig};

/// A parsed `#[derive(PostgresHash)]` item.
///
/// It should be used with [`syn::parse::Parse`] functions.
///
/// Using [`quote::ToTokens`] will output the declaration for a `pgrx::datum::pgrx_sql_entity_graph::InventoryPostgresHash`.
///
/// On structs:
///
/// ```rust
/// use syn::{Macro, parse::Parse, parse_quote, parse};
/// use quote::{quote, ToTokens};
/// use pgrx_sql_entity_graph::PostgresHash;
///
/// # fn main() -> eyre::Result<()> {
/// use pgrx_sql_entity_graph::CodeEnrichment;
/// let parsed: CodeEnrichment<PostgresHash> = parse_quote! {
///     #[derive(PostgresHash)]
///     struct Example<'a> {
///         demo: &'a str,
///     }
/// };
/// let sql_graph_entity_tokens = parsed.to_token_stream();
/// # Ok(())
/// # }
/// ```
///
/// On enums:
///
/// ```rust
/// use syn::{Macro, parse::Parse, parse_quote, parse};
/// use quote::{quote, ToTokens};
/// use pgrx_sql_entity_graph::PostgresHash;
///
/// # fn main() -> eyre::Result<()> {
/// use pgrx_sql_entity_graph::CodeEnrichment;
/// let parsed: CodeEnrichment<PostgresHash> = parse_quote! {
///     #[derive(PostgresHash)]
///     enum Demo {
///         Example,
///     }
/// };
/// let sql_graph_entity_tokens = parsed.to_token_stream();
/// # Ok(())
/// # }
/// ```
#[derive(Debug, Clone)]
pub struct PostgresHash {
    pub name: Ident,
    pub to_sql_config: ToSqlConfig,
}

impl PostgresHash {
    pub fn new(
        name: Ident,
        to_sql_config: ToSqlConfig,
    ) -> Result<CodeEnrichment<Self>, syn::Error> {
        if !to_sql_config.overrides_default() {
            crate::ident_is_acceptable_to_postgres(&name)?;
        }
        Ok(CodeEnrichment(Self { name, to_sql_config }))
    }

    pub fn from_derive_input(
        derive_input: DeriveInput,
    ) -> Result<CodeEnrichment<Self>, syn::Error> {
        let to_sql_config =
            ToSqlConfig::from_attributes(derive_input.attrs.as_slice())?.unwrap_or_default();
        Self::new(derive_input.ident, to_sql_config)
    }
}

impl ToEntityGraphTokens for PostgresHash {
    fn to_entity_graph_tokens(&self) -> TokenStream2 {
        let name = &self.name;
        let sql_graph_entity_fn_name = format_ident!("__pgrx_schema_hash_{}", self.name);
        let to_sql_config = &self.to_sql_config;
        let to_sql_config_len = to_sql_config.section_len_tokens();
        let payload_len = quote! {
            ::pgrx::pgrx_sql_entity_graph::section::u8_len()
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(stringify!(#name))
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(file!())
                + ::pgrx::pgrx_sql_entity_graph::section::u32_len()
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(stringify!(#name))
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(module_path!())
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(<#name as ::pgrx::pgrx_sql_entity_graph::metadata::SqlTranslatable>::TYPE_IDENT)
                + (#to_sql_config_len)
        };
        let total_len = quote! {
            ::pgrx::pgrx_sql_entity_graph::section::u32_len() + (#payload_len)
        };
        let writer = to_sql_config.section_writer_tokens(quote! {
            ::pgrx::pgrx_sql_entity_graph::section::EntryWriter::<{ #total_len }>::new()
                .u32((#payload_len) as u32)
                .u8(::pgrx::pgrx_sql_entity_graph::section::ENTITY_HASH)
                .str(stringify!(#name))
                .str(file!())
                .u32(line!())
                .str(stringify!(#name))
                .str(module_path!())
                .str(<#name as ::pgrx::pgrx_sql_entity_graph::metadata::SqlTranslatable>::TYPE_IDENT)
        });
        quote! {
            ::pgrx::pgrx_sql_entity_graph::__pgrx_schema_entry!(
                #sql_graph_entity_fn_name,
                #total_len,
                #writer.finish()
            );
        }
    }
}

impl ToRustCodeTokens for PostgresHash {}

impl Parse for CodeEnrichment<PostgresHash> {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        use syn::Item;

        let parsed = input.parse()?;
        let (ident, attrs) = match &parsed {
            Item::Enum(item) => (item.ident.clone(), item.attrs.as_slice()),
            Item::Struct(item) => (item.ident.clone(), item.attrs.as_slice()),
            _ => return Err(syn::Error::new(input.span(), "expected enum or struct")),
        };

        let to_sql_config = ToSqlConfig::from_attributes(attrs)?.unwrap_or_default();
        PostgresHash::new(ident, to_sql_config)
    }
}
