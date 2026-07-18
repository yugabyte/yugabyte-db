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

`#[derive(PostgresOrd)]` related macro expansion for Rust to SQL translation

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

/// A parsed `#[derive(PostgresOrd)]` item.
///
/// It should be used with [`syn::parse::Parse`] functions.
///
/// Using [`quote::ToTokens`] will output the declaration for a `pgrx::datum::pgrx_sql_entity_graph::InventoryPostgresOrd`.
///
/// On structs:
///
/// ```rust
/// use syn::{Macro, parse::Parse, parse_quote, parse};
/// use quote::{quote, ToTokens};
/// use pgrx_sql_entity_graph::PostgresOrd;
///
/// # fn main() -> eyre::Result<()> {
/// use pgrx_sql_entity_graph::CodeEnrichment;
/// let parsed: CodeEnrichment<PostgresOrd> = parse_quote! {
///     #[derive(PostgresOrd)]
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
/// use pgrx_sql_entity_graph::PostgresOrd;
///
/// # fn main() -> eyre::Result<()> {
/// use pgrx_sql_entity_graph::CodeEnrichment;
/// let parsed: CodeEnrichment<PostgresOrd> = parse_quote! {
///     #[derive(PostgresOrd)]
///     enum Demo {
///         Example,
///     }
/// };
/// let sql_graph_entity_tokens = parsed.to_token_stream();
/// # Ok(())
/// # }
/// ```
#[derive(Debug, Clone)]
pub struct PostgresOrd {
    pub name: Ident,
    pub to_sql_config: ToSqlConfig,
}

impl PostgresOrd {
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

impl ToEntityGraphTokens for PostgresOrd {
    fn to_entity_graph_tokens(&self) -> TokenStream2 {
        let name = &self.name;
        let sql_graph_entity_fn_name = format_ident!("__pgrx_internals_ord_{}", self.name);
        let to_sql_config = &self.to_sql_config;
        quote! {
            #[no_mangle]
            #[doc(hidden)]
            #[allow(nonstandard_style, unknown_lints, clippy::no_mangle_with_rust_abi)]
            pub extern "Rust" fn  #sql_graph_entity_fn_name() -> ::pgrx::pgrx_sql_entity_graph::SqlGraphEntity {
                use core::any::TypeId;
                extern crate alloc;
                use alloc::vec::Vec;
                use alloc::vec;
                let submission = ::pgrx::pgrx_sql_entity_graph::PostgresOrdEntity {
                    name: stringify!(#name),
                    file: file!(),
                    line: line!(),
                    full_path: core::any::type_name::<#name>(),
                    module_path: module_path!(),
                    id: TypeId::of::<#name>(),
                    to_sql_config: #to_sql_config,
                };
                ::pgrx::pgrx_sql_entity_graph::SqlGraphEntity::Ord(submission)
            }
        }
    }
}

impl ToRustCodeTokens for PostgresOrd {}

impl Parse for CodeEnrichment<PostgresOrd> {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        use syn::Item;

        let parsed = input.parse()?;
        let (ident, attrs) = match &parsed {
            Item::Enum(item) => (item.ident.clone(), item.attrs.as_slice()),
            Item::Struct(item) => (item.ident.clone(), item.attrs.as_slice()),
            _ => return Err(syn::Error::new(input.span(), "expected enum or struct")),
        };
        let to_sql_config = ToSqlConfig::from_attributes(attrs)?.unwrap_or_default();
        PostgresOrd::new(ident, to_sql_config)
    }
}
