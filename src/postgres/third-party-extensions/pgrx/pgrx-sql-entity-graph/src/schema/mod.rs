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

`#[pg_schema]` related macro expansion for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
pub mod entity;

use proc_macro2::TokenStream as TokenStream2;
use quote::{ToTokens, TokenStreamExt, quote};
use syn::ItemMod;
use syn::parse::{Parse, ParseStream};

/// A parsed `#[pg_schema] mod example {}` item.
///
/// It should be used with [`syn::parse::Parse`] functions.
///
/// Using [`quote::ToTokens`] will output the declaration for a `pgrx::datum::pgrx_sql_entity_graph::InventorySchema`.
///
/// ```rust
/// use syn::{Macro, parse::Parse, parse_quote, parse};
/// use quote::{quote, ToTokens};
/// use pgrx_sql_entity_graph::Schema;
///
/// # fn main() -> eyre::Result<()> {
/// let parsed: Schema = parse_quote! {
///     #[pg_schema] mod example {}
/// };
/// let entity_tokens = parsed.to_token_stream();
/// # Ok(())
/// # }
/// ```
#[derive(Debug, Clone)]
pub struct Schema {
    pub module: ItemMod,
}

impl Schema {
    /*
       It's necessary for `Schema` to handle the full `impl ToTokens` generation itself as the sql
       entity graph code has to be inside the same `mod {}` that the `#[pg_schema]` macro is
       attached to.

       To facilitate that, we feature flag the `.entity_tokens()` function here to be a no-op if
       the `no-schema-generation` feature flag is turned on
    */

    #[cfg(feature = "no-schema-generation")]
    fn entity_tokens(&self) -> TokenStream2 {
        quote! {}
    }

    #[cfg(not(feature = "no-schema-generation"))]
    fn entity_tokens(&self) -> TokenStream2 {
        let ident = &self.module.ident;
        let postfix = {
            use std::hash::{Hash, Hasher};

            let (_content_brace, content_items) =
                &self.module.content.as_ref().expect("Can only support `mod {}` right now.");

            // A hack until https://github.com/rust-lang/rust/issues/54725 is fixed.
            let mut hasher = std::collections::hash_map::DefaultHasher::new();
            content_items.hash(&mut hasher);
            hasher.finish()
            // End of hack
        };

        let sql_graph_entity_fn_name = quote::format_ident!("__pgrx_schema_{ident}_{postfix}");
        let payload_len = quote! {
            ::pgrx::pgrx_sql_entity_graph::section::u8_len()
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(module_path!())
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(stringify!(#ident))
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(file!())
                + ::pgrx::pgrx_sql_entity_graph::section::u32_len()
        };
        let total_len = quote! {
            ::pgrx::pgrx_sql_entity_graph::section::u32_len() + (#payload_len)
        };
        quote! {
            ::pgrx::pgrx_sql_entity_graph::__pgrx_schema_entry!(
                #sql_graph_entity_fn_name,
                #total_len,
                ::pgrx::pgrx_sql_entity_graph::section::EntryWriter::<{ #total_len }>::new()
                    .u32((#payload_len) as u32)
                    .u8(::pgrx::pgrx_sql_entity_graph::section::ENTITY_SCHEMA)
                    .str(module_path!())
                    .str(stringify!(#ident))
                    .str(file!())
                    .u32(line!())
                    .finish()
            );
        }
    }
}

// We can't use the `CodeEnrichment` infrastructure, so we implement [`ToTokens`] directly
impl ToTokens for Schema {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let attrs = &self.module.attrs;
        let vis = &self.module.vis;
        let mod_token = &self.module.mod_token;
        let ident = &self.module.ident;
        let graph_tokens = self.entity_tokens(); // NB:  this could be an empty TokenStream if `no-schema-generation` is turned on

        let (_content_brace, content_items) =
            &self.module.content.as_ref().expect("Can only support `mod {}` right now.");

        let code = quote! {
            #(#attrs)*
            #vis #mod_token #ident {
                #(#content_items)*
                #graph_tokens
            }
        };

        tokens.append_all(code)
    }
}

impl Parse for Schema {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        let module: ItemMod = input.parse()?;
        crate::ident_is_acceptable_to_postgres(&module.ident)?;
        Ok(Self { module })
    }
}
