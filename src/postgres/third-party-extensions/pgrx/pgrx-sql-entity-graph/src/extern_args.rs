//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::PositioningRef;
use proc_macro2::{Ident, Span, TokenStream, TokenTree};
use quote::{ToTokens, TokenStreamExt, format_ident, quote};
use std::collections::HashSet;

#[derive(Debug, Hash, Eq, PartialEq, Clone, PartialOrd, Ord)]
pub enum ExternArgs {
    CreateOrReplace,
    Immutable,
    Strict,
    Stable,
    Volatile,
    Raw,
    NoGuard,
    SecurityDefiner,
    SecurityInvoker,
    ParallelSafe,
    ParallelUnsafe,
    ParallelRestricted,
    ShouldPanic(String),
    Schema(String),
    Support(PositioningRef),
    Name(String),
    Cost(String),
    Requires(Vec<PositioningRef>),
}

impl core::fmt::Display for ExternArgs {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Self::CreateOrReplace => write!(f, "CREATE OR REPLACE"),
            Self::Immutable => write!(f, "IMMUTABLE"),
            Self::Strict => write!(f, "STRICT"),
            Self::Stable => write!(f, "STABLE"),
            Self::Volatile => write!(f, "VOLATILE"),
            Self::Raw => Ok(()),
            Self::ParallelSafe => write!(f, "PARALLEL SAFE"),
            Self::ParallelUnsafe => write!(f, "PARALLEL UNSAFE"),
            Self::SecurityDefiner => write!(f, "SECURITY DEFINER"),
            Self::SecurityInvoker => write!(f, "SECURITY INVOKER"),
            Self::ParallelRestricted => write!(f, "PARALLEL RESTRICTED"),
            Self::Support(item) => write!(f, "{item}"),
            Self::ShouldPanic(_) => Ok(()),
            Self::NoGuard => Ok(()),
            Self::Schema(_) => Ok(()),
            Self::Name(_) => Ok(()),
            Self::Cost(cost) => write!(f, "COST {cost}"),
            Self::Requires(_) => Ok(()),
        }
    }
}

impl ExternArgs {
    pub fn section_len_tokens(&self) -> TokenStream {
        match self {
            Self::CreateOrReplace
            | Self::Immutable
            | Self::Strict
            | Self::Stable
            | Self::Volatile
            | Self::Raw
            | Self::NoGuard
            | Self::SecurityDefiner
            | Self::SecurityInvoker
            | Self::ParallelSafe
            | Self::ParallelUnsafe
            | Self::ParallelRestricted => {
                quote! { ::pgrx::pgrx_sql_entity_graph::section::u8_len() }
            }
            Self::ShouldPanic(value)
            | Self::Schema(value)
            | Self::Name(value)
            | Self::Cost(value) => quote! {
                ::pgrx::pgrx_sql_entity_graph::section::u8_len()
                    + ::pgrx::pgrx_sql_entity_graph::section::str_len(#value)
            },
            Self::Support(item) => {
                let item_len = item.section_len_tokens();
                quote! {
                    ::pgrx::pgrx_sql_entity_graph::section::u8_len() + (#item_len)
                }
            }
            Self::Requires(items) => {
                let item_lens = items.iter().map(PositioningRef::section_len_tokens);
                quote! {
                    ::pgrx::pgrx_sql_entity_graph::section::u8_len()
                        + ::pgrx::pgrx_sql_entity_graph::section::list_len(&[
                            #( #item_lens ),*
                        ])
                }
            }
        }
    }

    pub fn section_writer_tokens(&self, writer: TokenStream) -> TokenStream {
        match self {
            Self::CreateOrReplace => {
                quote! { #writer.u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_CREATE_OR_REPLACE) }
            }
            Self::Immutable => {
                quote! { #writer.u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_IMMUTABLE) }
            }
            Self::Strict => {
                quote! { #writer.u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_STRICT) }
            }
            Self::Stable => {
                quote! { #writer.u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_STABLE) }
            }
            Self::Volatile => {
                quote! { #writer.u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_VOLATILE) }
            }
            Self::Raw => {
                quote! { #writer.u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_RAW) }
            }
            Self::NoGuard => {
                quote! { #writer.u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_NO_GUARD) }
            }
            Self::SecurityDefiner => quote! {
                #writer.u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_SECURITY_DEFINER)
            },
            Self::SecurityInvoker => quote! {
                #writer.u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_SECURITY_INVOKER)
            },
            Self::ParallelSafe => quote! {
                #writer.u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_PARALLEL_SAFE)
            },
            Self::ParallelUnsafe => quote! {
                #writer.u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_PARALLEL_UNSAFE)
            },
            Self::ParallelRestricted => quote! {
                #writer.u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_PARALLEL_RESTRICTED)
            },
            Self::ShouldPanic(value) => quote! {
                #writer
                    .u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_SHOULD_PANIC)
                    .str(#value)
            },
            Self::Schema(value) => quote! {
                #writer
                    .u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_SCHEMA)
                    .str(#value)
            },
            Self::Support(item) => item.section_writer_tokens(quote! {
                #writer.u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_SUPPORT)
            }),
            Self::Name(value) => quote! {
                #writer
                    .u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_NAME)
                    .str(#value)
            },
            Self::Cost(value) => quote! {
                #writer
                    .u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_COST)
                    .str(#value)
            },
            Self::Requires(items) => {
                let writer_ident = Ident::new("__pgrx_schema_writer", Span::mixed_site());
                let item_writers =
                    items.iter().map(|item| item.section_writer_tokens(quote! { #writer_ident }));
                let count = items.len();
                quote! {
                    {
                        let #writer_ident = #writer
                            .u8(::pgrx::pgrx_sql_entity_graph::section::EXTERN_ARG_REQUIRES)
                            .u32(#count as u32);
                        #( let #writer_ident = { #item_writers }; )*
                        #writer_ident
                    }
                }
            }
        }
    }
}

impl ToTokens for ExternArgs {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        match self {
            Self::CreateOrReplace => tokens.append(format_ident!("CreateOrReplace")),
            Self::Immutable => tokens.append(format_ident!("Immutable")),
            Self::Strict => tokens.append(format_ident!("Strict")),
            Self::Stable => tokens.append(format_ident!("Stable")),
            Self::Volatile => tokens.append(format_ident!("Volatile")),
            Self::Raw => tokens.append(format_ident!("Raw")),
            Self::NoGuard => tokens.append(format_ident!("NoGuard")),
            Self::SecurityDefiner => tokens.append(format_ident!("SecurityDefiner")),
            Self::SecurityInvoker => tokens.append(format_ident!("SecurityInvoker")),
            Self::ParallelSafe => tokens.append(format_ident!("ParallelSafe")),
            Self::ParallelUnsafe => tokens.append(format_ident!("ParallelUnsafe")),
            Self::ParallelRestricted => tokens.append(format_ident!("ParallelRestricted")),
            Self::ShouldPanic(_s) => tokens.append_all(quote! { Error(String::from("#_s")) }),
            Self::Schema(_s) => tokens.append_all(quote! { Schema(String::from("#_s")) }),
            Self::Support(item) => tokens.append_all(quote! { Support(#item) }),
            Self::Name(_s) => tokens.append_all(quote! { Name(String::from("#_s")) }),
            Self::Cost(_s) => tokens.append_all(quote! { Cost(String::from("#_s")) }),
            Self::Requires(items) => tokens.append_all(quote! { Requires(vec![#(#items),*]) }),
        }
    }
}

// This horror-story should be returning result
#[track_caller]
pub fn parse_extern_attributes(attr: TokenStream) -> HashSet<ExternArgs> {
    let mut args = HashSet::<ExternArgs>::new();
    let mut itr = attr.into_iter();
    while let Some(t) = itr.next() {
        match t {
            TokenTree::Group(g) => {
                for arg in parse_extern_attributes(g.stream()).into_iter() {
                    args.insert(arg);
                }
            }
            TokenTree::Ident(i) => {
                let name = i.to_string();
                match name.as_str() {
                    "create_or_replace" => args.insert(ExternArgs::CreateOrReplace),
                    "immutable" => args.insert(ExternArgs::Immutable),
                    "strict" => args.insert(ExternArgs::Strict),
                    "stable" => args.insert(ExternArgs::Stable),
                    "volatile" => args.insert(ExternArgs::Volatile),
                    "raw" => args.insert(ExternArgs::Raw),
                    "no_guard" => args.insert(ExternArgs::NoGuard),
                    "security_invoker" => args.insert(ExternArgs::SecurityInvoker),
                    "security_definer" => args.insert(ExternArgs::SecurityDefiner),
                    "parallel_safe" => args.insert(ExternArgs::ParallelSafe),
                    "parallel_unsafe" => args.insert(ExternArgs::ParallelUnsafe),
                    "parallel_restricted" => args.insert(ExternArgs::ParallelRestricted),
                    "error" | "expected" => {
                        let _punc = itr.next().unwrap();
                        let literal = itr.next().unwrap();
                        let message = literal.to_string();
                        let message = unescape::unescape(&message).expect("failed to unescape");

                        // trim leading/trailing quotes around the literal
                        let message = message[1..message.len() - 1].to_string();
                        args.insert(ExternArgs::ShouldPanic(message.to_string()))
                    }
                    "schema" => {
                        let _punc = itr.next().unwrap();
                        let literal = itr.next().unwrap();
                        let schema = literal.to_string();
                        let schema = unescape::unescape(&schema).expect("failed to unescape");

                        // trim leading/trailing quotes around the literal
                        let schema = schema[1..schema.len() - 1].to_string();
                        args.insert(ExternArgs::Schema(schema.to_string()))
                    }
                    "name" => {
                        let _punc = itr.next().unwrap();
                        let literal = itr.next().unwrap();
                        let name = literal.to_string();
                        let name = unescape::unescape(&name).expect("failed to unescape");

                        // trim leading/trailing quotes around the literal
                        let name = name[1..name.len() - 1].to_string();
                        args.insert(ExternArgs::Name(name.to_string()))
                    }
                    // Recognized, but not handled as an extern argument
                    "sql" => {
                        let _punc = itr.next().unwrap();
                        let _value = itr.next().unwrap();
                        false
                    }
                    _ => false,
                };
            }
            TokenTree::Punct(_) => {}
            TokenTree::Literal(_) => {}
        }
    }
    args
}

#[cfg(test)]
mod tests {
    use std::str::FromStr;

    use crate::{ExternArgs, parse_extern_attributes};

    #[test]
    fn parse_args() {
        let s = "error = \"syntax error at or near \\\"THIS\\\"\"";
        let ts = proc_macro2::TokenStream::from_str(s).unwrap();

        let args = parse_extern_attributes(ts);
        assert!(
            args.contains(&ExternArgs::ShouldPanic("syntax error at or near \"THIS\"".to_string()))
        );
    }
}
