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

`#[pg_extern]` related attributes for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use crate::positioning_ref::PositioningRef;
use crate::to_sql::ToSqlConfig;
use proc_macro2::TokenStream as TokenStream2;
use quote::{quote, ToTokens, TokenStreamExt};
use syn::parse::{Parse, ParseStream};
use syn::punctuated::Punctuated;
use syn::Token;

#[derive(Debug, Clone, Hash, Eq, PartialEq)]
pub enum Attribute {
    Immutable,
    Strict,
    Stable,
    Volatile,
    Raw,
    NoGuard,
    CreateOrReplace,
    SecurityDefiner,
    SecurityInvoker,
    ParallelSafe,
    ParallelUnsafe,
    ParallelRestricted,
    ShouldPanic(syn::LitStr),
    Schema(syn::LitStr),
    Name(syn::LitStr),
    Cost(syn::Expr),
    Requires(Punctuated<PositioningRef, Token![,]>),
    Sql(ToSqlConfig),
}

impl Attribute {
    pub(crate) fn to_sql_entity_graph_tokens(&self) -> TokenStream2 {
        match self {
            Attribute::Immutable => {
                quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::Immutable }
            }
            Attribute::Strict => quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::Strict },
            Attribute::Stable => quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::Stable },
            Attribute::Volatile => {
                quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::Volatile }
            }
            Attribute::Raw => quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::Raw },
            Attribute::NoGuard => quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::NoGuard },
            Attribute::CreateOrReplace => {
                quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::CreateOrReplace }
            }
            Attribute::SecurityDefiner => {
                quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::SecurityDefiner}
            }
            Attribute::SecurityInvoker => {
                quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::SecurityInvoker}
            }
            Attribute::ParallelSafe => {
                quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::ParallelSafe }
            }
            Attribute::ParallelUnsafe => {
                quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::ParallelUnsafe }
            }
            Attribute::ParallelRestricted => {
                quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::ParallelRestricted }
            }
            Attribute::ShouldPanic(s) => {
                quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::ShouldPanic(String::from(#s)) }
            }
            Attribute::Schema(s) => {
                quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::Schema(String::from(#s)) }
            }
            Attribute::Name(s) => {
                quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::Name(String::from(#s)) }
            }
            Attribute::Cost(s) => {
                quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::Cost(format!("{}", #s)) }
            }
            Attribute::Requires(items) => {
                let items_iter = items.iter().map(|x| x.to_token_stream()).collect::<Vec<_>>();
                quote! { ::pgrx::pgrx_sql_entity_graph::ExternArgs::Requires(vec![#(#items_iter),*],) }
            }
            // This attribute is handled separately
            Attribute::Sql(_) => {
                quote! {}
            }
        }
    }
}

impl ToTokens for Attribute {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let quoted = match self {
            Attribute::Immutable => quote! { immutable },
            Attribute::Strict => quote! { strict },
            Attribute::Stable => quote! { stable },
            Attribute::Volatile => quote! { volatile },
            Attribute::Raw => quote! { raw },
            Attribute::NoGuard => quote! { no_guard },
            Attribute::CreateOrReplace => quote! { create_or_replace },
            Attribute::SecurityDefiner => {
                quote! {security_definer}
            }
            Attribute::SecurityInvoker => {
                quote! {security_invoker}
            }
            Attribute::ParallelSafe => {
                quote! { parallel_safe }
            }
            Attribute::ParallelUnsafe => {
                quote! { parallel_unsafe }
            }
            Attribute::ParallelRestricted => {
                quote! { parallel_restricted }
            }
            Attribute::ShouldPanic(s) => {
                quote! { expected = #s }
            }
            Attribute::Schema(s) => {
                quote! { schema = #s }
            }
            Attribute::Name(s) => {
                quote! { name = #s }
            }
            Attribute::Cost(s) => {
                quote! { cost = #s }
            }
            Attribute::Requires(items) => {
                let items_iter = items.iter().map(|x| x.to_token_stream()).collect::<Vec<_>>();
                quote! { requires = [#(#items_iter),*] }
            }
            // This attribute is handled separately
            Attribute::Sql(to_sql_config) => {
                quote! { sql = #to_sql_config }
            }
        };
        tokens.append_all(quoted);
    }
}

impl Parse for Attribute {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        let ident: syn::Ident = input.parse()?;
        let found = match ident.to_string().as_str() {
            "immutable" => Self::Immutable,
            "strict" => Self::Strict,
            "stable" => Self::Stable,
            "volatile" => Self::Volatile,
            "raw" => Self::Raw,
            "no_guard" => Self::NoGuard,
            "create_or_replace" => Self::CreateOrReplace,
            "security_definer" => Self::SecurityDefiner,
            "security_invoker" => Self::SecurityInvoker,
            "parallel_safe" => Self::ParallelSafe,
            "parallel_unsafe" => Self::ParallelUnsafe,
            "parallel_restricted" => Self::ParallelRestricted,
            "error" | "expected" => {
                let _eq: Token![=] = input.parse()?;
                let literal: syn::LitStr = input.parse()?;
                Attribute::ShouldPanic(literal)
            }
            "schema" => {
                let _eq: Token![=] = input.parse()?;
                let literal: syn::LitStr = input.parse()?;
                Attribute::Schema(literal)
            }
            "name" => {
                let _eq: Token![=] = input.parse()?;
                let literal: syn::LitStr = input.parse()?;
                Self::Name(literal)
            }
            "cost" => {
                let _eq: Token![=] = input.parse()?;
                let literal: syn::Expr = input.parse()?;
                Self::Cost(literal)
            }
            "requires" => {
                let _eq: syn::token::Eq = input.parse()?;
                let content;
                let _bracket = syn::bracketed!(content in input);
                Self::Requires(content.parse_terminated(PositioningRef::parse, Token![,])?)
            }
            "sql" => {
                use crate::pgrx_attribute::ArgValue;
                use syn::Lit;

                let _eq: Token![=] = input.parse()?;
                match input.parse::<ArgValue>()? {
                    ArgValue::Path(p) => Self::Sql(ToSqlConfig::from(p)),
                    ArgValue::Lit(Lit::Bool(b)) => Self::Sql(ToSqlConfig::from(b.value)),
                    ArgValue::Lit(Lit::Str(s)) => Self::Sql(ToSqlConfig::from(s)),
                    ArgValue::Lit(other) => {
                        // FIXME: add a ui test for this
                        return Err(syn::Error::new(
                            other.span(),
                            "expected boolean, path, or string literal",
                        ));
                    }
                }
            }
            e => {
                // FIXME: add a UI test for this
                return Err(syn::Error::new(
                    ident.span(),
                    format!("Invalid option `{e}` inside `{ident} {input}`"),
                ));
            }
        };
        Ok(found)
    }
}
