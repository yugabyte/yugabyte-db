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
use quote::{ToTokens, TokenStreamExt, format_ident, quote};
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
        let mut requires: Vec<PositioningRef> = vec![];
        let mut creates: Vec<SqlDeclared> = vec![];
        for attr in &self.attrs {
            match attr {
                ExtensionSqlAttribute::Creates(items) => {
                    creates.extend(items.iter().cloned());
                }
                ExtensionSqlAttribute::Requires(items) => {
                    requires.extend(items.iter().cloned());
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
        let require_lens = requires.iter().map(PositioningRef::section_len_tokens);
        let create_lens = creates.iter().map(SqlDeclared::section_len_tokens);
        let require_writers =
            requires.iter().map(|item| item.section_writer_tokens(quote! { writer }));
        let create_writers =
            creates.iter().map(|item| item.section_writer_tokens(quote! { writer }));
        let require_count = requires.len();
        let create_count = creates.len();
        let sql_graph_entity_fn_name = format_ident!("__pgrx_schema_sql_{}", name.clone());
        let payload_len = quote! {
            ::pgrx::pgrx_sql_entity_graph::section::u8_len()
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(include_str!(#path))
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(module_path!())
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(concat!(file!(), ':', line!()))
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(file!())
                + ::pgrx::pgrx_sql_entity_graph::section::u32_len()
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(#name)
                + ::pgrx::pgrx_sql_entity_graph::section::bool_len()
                + ::pgrx::pgrx_sql_entity_graph::section::bool_len()
                + ::pgrx::pgrx_sql_entity_graph::section::list_len(&[
                    #( #require_lens ),*
                ])
                + ::pgrx::pgrx_sql_entity_graph::section::list_len(&[
                    #( #create_lens ),*
                ])
        };
        let total_len = quote! {
            ::pgrx::pgrx_sql_entity_graph::section::u32_len() + (#payload_len)
        };
        quote! {
            ::pgrx::pgrx_sql_entity_graph::__pgrx_schema_entry!(
                #sql_graph_entity_fn_name,
                #total_len,
                {
                    let writer = ::pgrx::pgrx_sql_entity_graph::section::EntryWriter::<{ #total_len }>::new()
                        .u32((#payload_len) as u32)
                        .u8(::pgrx::pgrx_sql_entity_graph::section::ENTITY_CUSTOM_SQL)
                        .str(include_str!(#path))
                        .str(module_path!())
                        .str(concat!(file!(), ':', line!()))
                        .str(file!())
                        .u32(line!())
                        .str(#name)
                        .bool(#bootstrap)
                        .bool(#finalize)
                        .u32(#require_count as u32);
                    #( let writer = { #require_writers }; )*
                    let writer = writer.u32(#create_count as u32);
                    #( let writer = { #create_writers }; )*
                    writer.finish()
                }
            );
        }
    }
}

impl ToRustCodeTokens for ExtensionSqlFile {}

impl Parse for CodeEnrichment<ExtensionSqlFile> {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        let path = input.parse()?;
        let _after_sql_comma: Option<Token![,]> = input.parse()?;
        let attrs = input.parse_terminated(ExtensionSqlAttribute::parse, Token![,])?;
        Ok(Self(ExtensionSqlFile { path, attrs }))
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
        let mut creates: Vec<SqlDeclared> = vec![];
        let mut requires: Vec<PositioningRef> = vec![];
        for attr in &self.attrs {
            match attr {
                ExtensionSqlAttribute::Requires(items) => {
                    requires.extend(items.iter().cloned());
                }
                ExtensionSqlAttribute::Creates(items) => {
                    creates.extend(items.iter().cloned());
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
        let name = &self.name;
        let require_lens = requires.iter().map(PositioningRef::section_len_tokens);
        let create_lens = creates.iter().map(SqlDeclared::section_len_tokens);
        let require_writers =
            requires.iter().map(|item| item.section_writer_tokens(quote! { writer }));
        let create_writers =
            creates.iter().map(|item| item.section_writer_tokens(quote! { writer }));
        let require_count = requires.len();
        let create_count = creates.len();
        let sql_graph_entity_fn_name = format_ident!("__pgrx_schema_sql_{}", name.value());
        let payload_len = quote! {
            ::pgrx::pgrx_sql_entity_graph::section::u8_len()
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(#sql)
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(module_path!())
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(concat!(file!(), ':', line!()))
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(file!())
                + ::pgrx::pgrx_sql_entity_graph::section::u32_len()
                + ::pgrx::pgrx_sql_entity_graph::section::str_len(#name)
                + ::pgrx::pgrx_sql_entity_graph::section::bool_len()
                + ::pgrx::pgrx_sql_entity_graph::section::bool_len()
                + ::pgrx::pgrx_sql_entity_graph::section::list_len(&[
                    #( #require_lens ),*
                ])
                + ::pgrx::pgrx_sql_entity_graph::section::list_len(&[
                    #( #create_lens ),*
                ])
        };
        let total_len = quote! {
            ::pgrx::pgrx_sql_entity_graph::section::u32_len() + (#payload_len)
        };
        quote! {
            ::pgrx::pgrx_sql_entity_graph::__pgrx_schema_entry!(
                #sql_graph_entity_fn_name,
                #total_len,
                {
                    let writer = ::pgrx::pgrx_sql_entity_graph::section::EntryWriter::<{ #total_len }>::new()
                        .u32((#payload_len) as u32)
                        .u8(::pgrx::pgrx_sql_entity_graph::section::ENTITY_CUSTOM_SQL)
                        .str(#sql)
                        .str(module_path!())
                        .str(concat!(file!(), ':', line!()))
                        .str(file!())
                        .u32(line!())
                        .str(#name)
                        .bool(#bootstrap)
                        .bool(#finalize)
                        .u32(#require_count as u32);
                    #( let writer = { #require_writers }; )*
                    let writer = writer.u32(#create_count as u32);
                    #( let writer = { #create_writers }; )*
                    writer.finish()
                }
            );
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
        Ok(Self(ExtensionSql { sql, attrs, name }))
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
                ));
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
            Self::Type(val) => ("Type", val),
            Self::Enum(val) => ("Enum", val),
            Self::Function(val) => ("Function", val),
        };
        let identifier_expr = self.section_identifier_tokens();
        match self {
            Self::Type(_) | Self::Enum(_) => {
                let identifier_path: syn::Path =
                    syn::parse_str(identifier).expect("type declaration path should parse");
                quote! {
                    ::pgrx::pgrx_sql_entity_graph::SqlDeclaredEntity::build_type::<#identifier_path>(#variant, #identifier_expr).unwrap()
                }
            }
            Self::Function(_) => quote! {
                ::pgrx::pgrx_sql_entity_graph::SqlDeclaredEntity::build(#variant, #identifier_expr).unwrap()
            },
        }
    }
}

impl ToRustCodeTokens for SqlDeclared {}

impl SqlDeclared {
    fn section_identifier_tokens(&self) -> TokenStream2 {
        let identifier = match self {
            Self::Type(value) | Self::Enum(value) | Self::Function(value) => value,
        };
        let identifier_split = identifier.split("::").collect::<Vec<_>>();
        if identifier_split.len() == 1 {
            let identifier_infer =
                Ident::new(identifier_split.last().unwrap(), proc_macro2::Span::call_site());
            quote! { concat!(module_path!(), "::", stringify!(#identifier_infer)) }
        } else {
            quote! { #identifier }
        }
    }

    pub fn section_len_tokens(&self) -> TokenStream2 {
        let identifier_expr = self.section_identifier_tokens();
        match self {
            Self::Type(identifier) | Self::Enum(identifier) => {
                let identifier_path: syn::Path =
                    syn::parse_str(identifier).expect("type declaration path should parse");
                quote! {
                    ::pgrx::pgrx_sql_entity_graph::section::u8_len()
                        + ::pgrx::pgrx_sql_entity_graph::section::str_len(#identifier_expr)
                        + ::pgrx::pgrx_sql_entity_graph::section::str_len(
                            <#identifier_path as ::pgrx::pgrx_sql_entity_graph::metadata::SqlTranslatable>::TYPE_IDENT
                        )
                        + ::pgrx::pgrx_sql_entity_graph::section::argument_sql_len(
                            <#identifier_path as ::pgrx::pgrx_sql_entity_graph::metadata::SqlTranslatable>::ARGUMENT_SQL
                        )
                }
            }
            Self::Function(_) => quote! {
                ::pgrx::pgrx_sql_entity_graph::section::u8_len()
                    + ::pgrx::pgrx_sql_entity_graph::section::str_len(#identifier_expr)
            },
        }
    }

    pub fn section_writer_tokens(&self, writer: TokenStream2) -> TokenStream2 {
        let identifier_expr = self.section_identifier_tokens();
        match self {
            Self::Type(identifier) => {
                let identifier_path: syn::Path =
                    syn::parse_str(identifier).expect("type declaration path should parse");
                quote! {
                    #writer
                        .u8(::pgrx::pgrx_sql_entity_graph::section::SQL_DECLARED_TYPE)
                        .str(#identifier_expr)
                        .str(<#identifier_path as ::pgrx::pgrx_sql_entity_graph::metadata::SqlTranslatable>::TYPE_IDENT)
                        .argument_sql(<#identifier_path as ::pgrx::pgrx_sql_entity_graph::metadata::SqlTranslatable>::ARGUMENT_SQL)
                }
            }
            Self::Enum(identifier) => {
                let identifier_path: syn::Path =
                    syn::parse_str(identifier).expect("type declaration path should parse");
                quote! {
                    #writer
                        .u8(::pgrx::pgrx_sql_entity_graph::section::SQL_DECLARED_ENUM)
                        .str(#identifier_expr)
                        .str(<#identifier_path as ::pgrx::pgrx_sql_entity_graph::metadata::SqlTranslatable>::TYPE_IDENT)
                        .argument_sql(<#identifier_path as ::pgrx::pgrx_sql_entity_graph::metadata::SqlTranslatable>::ARGUMENT_SQL)
                }
            }
            Self::Function(_) => quote! {
                #writer
                    .u8(::pgrx::pgrx_sql_entity_graph::section::SQL_DECLARED_FUNCTION)
                    .str(#identifier_expr)
            },
        }
    }
}

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
            "Type" => Self::Type(identifier_str),
            "Enum" => Self::Enum(identifier_str),
            "Function" => Self::Function(identifier_str),
            _ => {
                return Err(syn::Error::new(
                    variant.span(),
                    "SQL declared entities must be `Type(ident)`, `Enum(ident)`, or `Function(ident)`",
                ));
            }
        };
        Ok(this)
    }
}

impl ToTokens for SqlDeclared {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        tokens.append_all(self.to_entity_graph_tokens())
    }
}
