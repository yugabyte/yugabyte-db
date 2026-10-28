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

`sql = ...` fragment related macro expansion for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
pub mod entity;

use std::hash::Hash;

use proc_macro2::TokenStream as TokenStream2;
use quote::{ToTokens, TokenStreamExt, quote};
use syn::spanned::Spanned;
use syn::{AttrStyle, Attribute, Lit};

use crate::pgrx_attribute::{ArgValue, PgrxArg, PgrxAttribute};
use crate::pgrx_sql::PgrxSql;

/// Able to be transformed into to SQL.
pub trait ToSql {
    /// Attempt to transform this type into SQL.
    ///
    /// Some entities require additional context from a [`PgrxSql`], such as
    /// `#[derive(PostgresType)]` which must include it's relevant in/out functions.
    fn to_sql(&self, context: &PgrxSql) -> eyre::Result<String>;
}

/// A parsed `sql` option from a `pgrx` related procedural macro.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct ToSqlConfig {
    pub enabled: bool,
    pub content: Option<syn::LitStr>,
}
impl From<bool> for ToSqlConfig {
    fn from(enabled: bool) -> Self {
        Self { enabled, content: None }
    }
}
impl From<syn::LitStr> for ToSqlConfig {
    fn from(content: syn::LitStr) -> Self {
        Self { enabled: true, content: Some(content) }
    }
}
impl Default for ToSqlConfig {
    fn default() -> Self {
        Self { enabled: true, content: None }
    }
}

const INVALID_ATTR_CONTENT: &str =
    "expected `#[pgrx(sql = content)]`, where `content` is a boolean or string literal";

impl ToSqlConfig {
    /// Used for general purpose parsing from an attribute
    pub fn from_attribute(attr: &Attribute) -> Result<Option<Self>, syn::Error> {
        if attr.style != AttrStyle::Outer {
            return Err(syn::Error::new(
                attr.span(),
                "#[pgrx(sql = ..)] is only valid in an outer context",
            ));
        }

        let attr = attr.parse_args::<PgrxAttribute>()?;
        for arg in attr.args.iter() {
            let PgrxArg::NameValue(nv) = arg;
            if !nv.path.is_ident("sql") {
                continue;
            }

            return match nv.value {
                ArgValue::Lit(Lit::Bool(ref b)) => {
                    Ok(Some(Self { enabled: b.value, content: None }))
                }
                ArgValue::Lit(Lit::Str(ref s)) => {
                    Ok(Some(Self { enabled: true, content: Some(s.clone()) }))
                }
                ArgValue::Path(ref path) => Err(syn::Error::new(path.span(), INVALID_ATTR_CONTENT)),
                ArgValue::Lit(ref other) => {
                    Err(syn::Error::new(other.span(), INVALID_ATTR_CONTENT))
                }
            };
        }

        Ok(None)
    }

    /// Used to parse a generator config from a set of item attributes
    pub fn from_attributes(attrs: &[Attribute]) -> Result<Option<Self>, syn::Error> {
        if let Some(attr) = attrs.iter().find(|attr| attr.path().is_ident("pgrx")) {
            Self::from_attribute(attr)
        } else {
            Ok(None)
        }
    }

    pub fn overrides_default(&self) -> bool {
        !self.enabled || self.content.is_some()
    }

    pub fn section_len_tokens(&self) -> TokenStream2 {
        let content = &self.content;
        match content {
            Some(content) => quote! {
                ::pgrx::pgrx_sql_entity_graph::section::bool_len()
                    + ::pgrx::pgrx_sql_entity_graph::section::bool_len()
                    + ::pgrx::pgrx_sql_entity_graph::section::str_len(#content)
            },
            None => quote! {
                ::pgrx::pgrx_sql_entity_graph::section::bool_len()
                    + ::pgrx::pgrx_sql_entity_graph::section::bool_len()
            },
        }
    }

    pub fn section_writer_tokens(&self, writer: TokenStream2) -> TokenStream2 {
        let enabled = self.enabled;
        let content = &self.content;
        match content {
            Some(content) => quote! {
                #writer
                    .bool(#enabled)
                    .bool(true)
                    .str(#content)
            },
            None => quote! {
                #writer
                    .bool(#enabled)
                    .bool(false)
            },
        }
    }
}

impl ToTokens for ToSqlConfig {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let enabled = self.enabled;
        let content = &self.content;
        if let Some(sql) = content {
            tokens.append_all(quote! {
                ::pgrx::pgrx_sql_entity_graph::ToSqlConfigEntity {
                    enabled: #enabled,
                    content: Some(#sql),
                }
            });
            return;
        }
        tokens.append_all(quote! {
            ::pgrx::pgrx_sql_entity_graph::ToSqlConfigEntity {
                enabled: #enabled,
                content: None,
            }
        });
    }
}
