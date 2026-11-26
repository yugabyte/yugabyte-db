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
use quote::{ToTokens, TokenStreamExt, quote};
use syn::Token;
use syn::parse::{Parse, ParseStream};
use syn::punctuated::Punctuated;
use syn::spanned::Spanned;

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
    Support(PositioningRef),
    Name(syn::LitStr),
    Cost(Box<syn::Expr>),
    Requires(Punctuated<PositioningRef, Token![,]>),
    Sql(ToSqlConfig),
}

impl ToTokens for Attribute {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let quoted = match self {
            Self::Immutable => quote! { immutable },
            Self::Strict => quote! { strict },
            Self::Stable => quote! { stable },
            Self::Volatile => quote! { volatile },
            Self::Raw => quote! { raw },
            Self::NoGuard => quote! { no_guard },
            Self::CreateOrReplace => quote! { create_or_replace },
            Self::SecurityDefiner => {
                quote! {security_definer}
            }
            Self::SecurityInvoker => {
                quote! {security_invoker}
            }
            Self::ParallelSafe => {
                quote! { parallel_safe }
            }
            Self::ParallelUnsafe => {
                quote! { parallel_unsafe }
            }
            Self::ParallelRestricted => {
                quote! { parallel_restricted }
            }
            Self::ShouldPanic(s) => {
                quote! { expected = #s }
            }
            Self::Schema(s) => {
                quote! { schema = #s }
            }
            Self::Support(item) => {
                quote! { support = #item }
            }
            Self::Name(s) => {
                quote! { name = #s }
            }
            Self::Cost(s) => {
                quote! { cost = #s }
            }
            Self::Requires(items) => {
                let items_iter = items.iter().map(|x| x.to_token_stream()).collect::<Vec<_>>();
                quote! { requires = [#(#items_iter),*] }
            }
            // This attribute is handled separately
            Self::Sql(to_sql_config) => {
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
                Self::ShouldPanic(literal)
            }
            "schema" => {
                let _eq: Token![=] = input.parse()?;
                let literal: syn::LitStr = input.parse()?;
                Self::Schema(literal)
            }
            "support" => {
                let _eq: Token![=] = input.parse()?;
                let item: PositioningRef = input.parse()?;
                Self::Support(item)
            }
            "name" => {
                let _eq: Token![=] = input.parse()?;
                let literal: syn::LitStr = input.parse()?;
                Self::Name(literal)
            }
            "cost" => {
                let _eq: Token![=] = input.parse()?;
                let literal: syn::Expr = input.parse()?;
                Self::Cost(Box::new(literal))
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
                    ArgValue::Path(path) => {
                        return Err(syn::Error::new(
                            path.span(),
                            "expected boolean or string literal",
                        ));
                    }
                    ArgValue::Lit(Lit::Bool(b)) => Self::Sql(ToSqlConfig::from(b.value)),
                    ArgValue::Lit(Lit::Str(s)) => Self::Sql(ToSqlConfig::from(s)),
                    ArgValue::Lit(other) => {
                        // FIXME: add a ui test for this
                        return Err(syn::Error::new(
                            other.span(),
                            "expected boolean or string literal",
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
