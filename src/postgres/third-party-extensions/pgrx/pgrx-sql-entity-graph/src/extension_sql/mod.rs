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

`pgrx::extension_sql!()` related macro expansion for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.


*/
pub mod entity;

use crate::positioning_ref::PositioningRef;

use crate::enrich::{CodeEnrichment, ToEntityGraphTokens, ToRustCodeTokens};
use proc_macro2::{Ident, TokenStream as TokenStream2};
use quote::{format_ident, quote, ToTokens, TokenStreamExt};
use syn::parse::{Parse, ParseStream};
use syn::punctuated::Punctuated;
use syn::{LitStr, Token};

/// A parsed `extension_sql_file!()` item.
///
/// It should be used with [`syn::parse::Parse`] functions.
///
/// Using [`quote::ToTokens`] will output the declaration for a [`ExtensionSqlEntity`][crate::ExtensionSqlEntity].
///
/// ```rust
/// use syn::{Macro, parse::Parse, parse_quote, parse};
/// use quote::{quote, ToTokens};
/// use pgrx_sql_entity_graph::ExtensionSqlFile;
///
/// # fn main() -> eyre::Result<()> {
/// use pgrx_sql_entity_graph::CodeEnrichment;
/// let parsed: Macro = parse_quote! {
///     extension_sql_file!("sql/example.sql", name = "example", bootstrap)
/// };
/// let inner_tokens = parsed.tokens;
/// let inner: CodeEnrichment<ExtensionSqlFile> = parse_quote! {
///     #inner_tokens
/// };
/// let sql_graph_entity_tokens = inner.to_token_stream();
/// # Ok(())
/// # }
/// ```
#[derive(Debug, Clone)]
pub struct ExtensionSqlFile {
    pub path: LitStr,
    pub attrs: Punctuated<ExtensionSqlAttribute, Token![,]>,
}

impl ToEntityGraphTokens for ExtensionSqlFile {
    fn to_entity_graph_tokens(&self) -> TokenStream2 {
        let path = &self.path;
        let mut name = None;
        let mut bootstrap = false;
        let mut finalize = false;
        let mut requires = vec![];
        let mut creates = vec![];
        for attr in &self.attrs {
            match attr {
                ExtensionSqlAttribute::Creates(items) => {
                    creates.append(&mut items.iter().map(|x| x.to_token_stream()).collect());
                }
                ExtensionSqlAttribute::Requires(items) => {
                    requires.append(&mut items.iter().map(|x| x.to_token_stream()).collect());
                }
                ExtensionSqlAttribute::Bootstrap => {
                    bootstrap = true;
                }
                ExtensionSqlAttribute::Finalize => {
                    finalize = true;
                }
                ExtensionSqlAttribute::Name(found_name) => {
                    name = Some(found_name.value());
                }
            }
        }
        let name = name.unwrap_or(
            std::path::PathBuf::from(path.value())
                .file_stem()
                .expect("No file name for extension_sql_file!()")
                .to_str()
                .expect("No UTF-8 file name for extension_sql_file!()")
                .to_string(),
        );
        let requires_iter = requires.iter();
        let creates_iter = creates.iter();
        let sql_graph_entity_fn_name = format_ident!("__pgrx_internals_sql_{}", name.clone());
        quote! {
            #[no_mangle]
            #[doc(hidden)]
            #[allow(unknown_lints, clippy::no_mangle_with_rust_abi)]
            pub extern "Rust" fn  #sql_graph_entity_fn_name() -> ::pgrx::pgrx_sql_entity_graph::SqlGraphEntity {
                extern crate alloc;
                use alloc::vec::Vec;
                use alloc::vec;
                let submission = ::pgrx::pgrx_sql_entity_graph::ExtensionSqlEntity {
                    sql: include_str!(#path),
                    module_path: module_path!(),
                    full_path: concat!(file!(), ':', line!()),
                    file: file!(),
                    line: line!(),
                    name: #name,
                    bootstrap: #bootstrap,
                    finalize: #finalize,
                    requires: vec![#(#requires_iter),*],
                    creates: vec![#(#creates_iter),*],
                };
                ::pgrx::pgrx_sql_entity_graph::SqlGraphEntity::CustomSql(submission)
            }
        }
    }
}

impl ToRustCodeTokens for ExtensionSqlFile {}

impl Parse for CodeEnrichment<ExtensionSqlFile> {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        let path = input.parse()?;
        let _after_sql_comma: Option<Token![,]> = input.parse()?;
        let attrs = input.parse_terminated(ExtensionSqlAttribute::parse, Token![,])?;
        Ok(CodeEnrichment(ExtensionSqlFile { path, attrs }))
    }
}

/// A parsed `extension_sql!()` item.
///
/// It should be used with [`syn::parse::Parse`] functions.
///
/// Using [`quote::ToTokens`] will output the declaration for a `pgrx::pgrx_sql_entity_graph::ExtensionSqlEntity`.
///
/// ```rust
/// use syn::{Macro, parse::Parse, parse_quote, parse};
/// use quote::{quote, ToTokens};
/// use pgrx_sql_entity_graph::ExtensionSql;
///
/// # fn main() -> eyre::Result<()> {
/// use pgrx_sql_entity_graph::CodeEnrichment;
/// let parsed: Macro = parse_quote! {
///     extension_sql!("-- Example content", name = "example", bootstrap)
/// };
/// let inner_tokens = parsed.tokens;
/// let inner: CodeEnrichment<ExtensionSql> = parse_quote! {
///     #inner_tokens
/// };
/// let sql_graph_entity_tokens = inner.to_token_stream();
/// # Ok(())
/// # }
/// ```
#[derive(Debug, Clone)]
pub struct ExtensionSql {
    pub sql: LitStr,
    pub name: LitStr,
    pub attrs: Punctuated<ExtensionSqlAttribute, Token![,]>,
}

impl ToEntityGraphTokens for ExtensionSql {
    fn to_entity_graph_tokens(&self) -> TokenStream2 {
        let sql = &self.sql;
        let mut bootstrap = false;
        let mut finalize = false;
        let mut creates = vec![];
        let mut requires = vec![];
        for attr in &self.attrs {
            match attr {
                ExtensionSqlAttribute::Requires(items) => {
                    requires.append(&mut items.iter().map(|x| x.to_token_stream()).collect());
                }
                ExtensionSqlAttribute::Creates(items) => {
                    creates.append(&mut items.iter().map(|x| x.to_token_stream()).collect());
                }
                ExtensionSqlAttribute::Bootstrap => {
                    bootstrap = true;
                }
                ExtensionSqlAttribute::Finalize => {
                    finalize = true;
                }
                ExtensionSqlAttribute::Name(_found_name) => (), // Already done
            }
        }
        let requires_iter = requires.iter();
        let creates_iter = creates.iter();
        let name = &self.name;

        let sql_graph_entity_fn_name = format_ident!("__pgrx_internals_sql_{}", name.value());
        quote! {
            #[no_mangle]
            #[doc(hidden)]
            #[allow(unknown_lints, clippy::no_mangle_with_rust_abi)]
            pub extern "Rust" fn  #sql_graph_entity_fn_name() -> ::pgrx::pgrx_sql_entity_graph::SqlGraphEntity {
                extern crate alloc;
                use alloc::vec::Vec;
                use alloc::vec;
                let submission = ::pgrx::pgrx_sql_entity_graph::ExtensionSqlEntity {
                    sql: #sql,
                    module_path: module_path!(),
                    full_path: concat!(file!(), ':', line!()),
                    file: file!(),
                    line: line!(),
                    name: #name,
                    bootstrap: #bootstrap,
                    finalize: #finalize,
                    requires: vec![#(#requires_iter),*],
                    creates: vec![#(#creates_iter),*],
                };
                ::pgrx::pgrx_sql_entity_graph::SqlGraphEntity::CustomSql(submission)
            }
        }
    }
}

impl ToRustCodeTokens for ExtensionSql {}

impl Parse for CodeEnrichment<ExtensionSql> {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        let sql = input.parse()?;
        let _after_sql_comma: Option<Token![,]> = input.parse()?;
        let attrs = input.parse_terminated(ExtensionSqlAttribute::parse, Token![,])?;
        let name = attrs.iter().rev().find_map(|attr| match attr {
            ExtensionSqlAttribute::Name(found_name) => Some(found_name.clone()),
            _ => None,
        });
        let name =
            name.ok_or_else(|| syn::Error::new(input.span(), "expected `name` to be set"))?;
        Ok(CodeEnrichment(ExtensionSql { sql, attrs, name }))
    }
}

impl ToTokens for ExtensionSql {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        tokens.append_all(self.to_entity_graph_tokens())
    }
}

#[derive(Debug, Clone)]
pub enum ExtensionSqlAttribute {
    Requires(Punctuated<PositioningRef, Token![,]>),
    Creates(Punctuated<SqlDeclared, Token![,]>),
    Bootstrap,
    Finalize,
    Name(LitStr),
}

impl Parse for ExtensionSqlAttribute {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        let ident: Ident = input.parse()?;
        let found = match ident.to_string().as_str() {
            "creates" => {
                let _eq: syn::token::Eq = input.parse()?;
                let content;
                let _bracket = syn::bracketed!(content in input);
                Self::Creates(content.parse_terminated(SqlDeclared::parse, Token![,])?)
            }
            "requires" => {
                let _eq: syn::token::Eq = input.parse()?;
                let content;
                let _bracket = syn::bracketed!(content in input);
                Self::Requires(content.parse_terminated(PositioningRef::parse, Token![,])?)
            }
            "bootstrap" => Self::Bootstrap,
            "finalize" => Self::Finalize,
            "name" => {
                let _eq: syn::token::Eq = input.parse()?;
                Self::Name(input.parse()?)
            }
            other => {
                return Err(syn::Error::new(
                    ident.span(),
                    format!("Unknown extension_sql attribute: {other}"),
                ))
            }
        };
        Ok(found)
    }
}

#[derive(Debug, Clone, Hash, PartialEq, Eq, Ord, PartialOrd)]
pub enum SqlDeclared {
    Type(String),
    Enum(String),
    Function(String),
}

impl ToEntityGraphTokens for SqlDeclared {
    fn to_entity_graph_tokens(&self) -> TokenStream2 {
        let (variant, identifier) = match &self {
            SqlDeclared::Type(val) => ("Type", val),
            SqlDeclared::Enum(val) => ("Enum", val),
            SqlDeclared::Function(val) => ("Function", val),
        };
        let identifier_split = identifier.split("::").collect::<Vec<_>>();
        let identifier = if identifier_split.len() == 1 {
            let identifier_infer =
                Ident::new(identifier_split.last().unwrap(), proc_macro2::Span::call_site());
            quote! { concat!(module_path!(), "::", stringify!(#identifier_infer)) }
        } else {
            quote! { stringify!(#identifier) }
        };
        quote! {
            ::pgrx::pgrx_sql_entity_graph::SqlDeclaredEntity::build(#variant, #identifier).unwrap()
        }
    }
}

impl ToRustCodeTokens for SqlDeclared {}

impl Parse for SqlDeclared {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let variant: Ident = input.parse()?;
        let content;
        let _bracket: syn::token::Paren = syn::parenthesized!(content in input);
        let identifier_path: syn::Path = content.parse()?;
        let identifier_str = {
            let mut identifier_segments = Vec::new();
            for segment in identifier_path.segments {
                identifier_segments.push(segment.ident.to_string())
            }
            identifier_segments.join("::")
        };
        let this = match variant.to_string().as_str() {
            "Type" => SqlDeclared::Type(identifier_str),
            "Enum" => SqlDeclared::Enum(identifier_str),
            "Function" => SqlDeclared::Function(identifier_str),
            _ => return Err(syn::Error::new(
                variant.span(),
                "SQL declared entities must be `Type(ident)`, `Enum(ident)`, or `Function(ident)`",
            )),
        };
        Ok(this)
    }
}

impl ToTokens for SqlDeclared {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        tokens.append_all(self.to_entity_graph_tokens())
    }
}
