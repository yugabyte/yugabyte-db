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
use quote::{quote, ToTokens, TokenStreamExt};
use syn::spanned::Spanned;
use syn::{AttrStyle, Attribute, Lit};

use crate::pgrx_attribute::{ArgValue, PgrxArg, PgrxAttribute};
use crate::pgrx_sql::PgrxSql;
use crate::SqlGraphEntity;

/// Able to be transformed into to SQL.
pub trait ToSql {
    /// Attempt to transform this type into SQL.
    ///
    /// Some entities require additional context from a [`PgrxSql`], such as
    /// `#[derive(PostgresType)]` which must include it's relevant in/out functions.
    fn to_sql(&self, context: &PgrxSql) -> eyre::Result<String>;
}

/// The signature of a function that can transform a SqlGraphEntity to a SQL string
///
/// This is used to provide a facility for overriding the default SQL generator behavior using
/// the `#[to_sql(path::to::function)]` attribute in circumstances where the default behavior is
/// not desirable.
///
/// Implementations can invoke `ToSql::to_sql(entity, context)` on the unwrapped SqlGraphEntity
/// type should they wish to delegate to the default behavior for any reason.
pub type ToSqlFn =
    fn(
        &SqlGraphEntity,
        &PgrxSql,
    ) -> std::result::Result<String, Box<dyn std::error::Error + Send + Sync + 'static>>;

/// A parsed `sql` option from a `pgrx` related procedural macro.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct ToSqlConfig {
    pub enabled: bool,
    pub callback: Option<syn::Path>,
    pub content: Option<syn::LitStr>,
}
impl From<bool> for ToSqlConfig {
    fn from(enabled: bool) -> Self {
        Self { enabled, callback: None, content: None }
    }
}
impl From<syn::Path> for ToSqlConfig {
    fn from(path: syn::Path) -> Self {
        Self { enabled: true, callback: Some(path), content: None }
    }
}
impl From<syn::LitStr> for ToSqlConfig {
    fn from(content: syn::LitStr) -> Self {
        Self { enabled: true, callback: None, content: Some(content) }
    }
}
impl Default for ToSqlConfig {
    fn default() -> Self {
        Self { enabled: true, callback: None, content: None }
    }
}

const INVALID_ATTR_CONTENT: &str =
    "expected `#[pgrx(sql = content)]`, where `content` is a boolean, string, or path to a function";

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
            let PgrxArg::NameValue(ref nv) = arg;
            if !nv.path.is_ident("sql") {
                continue;
            }

            return match nv.value {
                ArgValue::Path(ref callback_path) => Ok(Some(Self {
                    enabled: true,
                    callback: Some(callback_path.clone()),
                    content: None,
                })),
                ArgValue::Lit(Lit::Bool(ref b)) => {
                    Ok(Some(Self { enabled: b.value, callback: None, content: None }))
                }
                ArgValue::Lit(Lit::Str(ref s)) => {
                    Ok(Some(Self { enabled: true, callback: None, content: Some(s.clone()) }))
                }
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
        !self.enabled || self.callback.is_some() || self.content.is_some()
    }
}

impl ToTokens for ToSqlConfig {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let enabled = self.enabled;
        let callback = &self.callback;
        let content = &self.content;
        if let Some(callback_path) = callback {
            tokens.append_all(quote! {
                ::pgrx::pgrx_sql_entity_graph::ToSqlConfigEntity {
                    enabled: #enabled,
                    callback: Some(#callback_path),
                    content: None,
                }
            });
            return;
        }
        if let Some(sql) = content {
            tokens.append_all(quote! {
                ::pgrx::pgrx_sql_entity_graph::ToSqlConfigEntity {
                    enabled: #enabled,
                    callback: None,
                    content: Some(#sql),
                }
            });
            return;
        }
        tokens.append_all(quote! {
            ::pgrx::pgrx_sql_entity_graph::ToSqlConfigEntity {
                enabled: #enabled,
                callback: None,
                content: None,
            }
        });
    }
}
