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
use proc_macro2::{TokenStream, TokenTree};
use quote::{format_ident, quote, ToTokens, TokenStreamExt};
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
    Name(String),
    Cost(String),
    Requires(Vec<PositioningRef>),
}

impl core::fmt::Display for ExternArgs {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            ExternArgs::CreateOrReplace => write!(f, "CREATE OR REPLACE"),
            ExternArgs::Immutable => write!(f, "IMMUTABLE"),
            ExternArgs::Strict => write!(f, "STRICT"),
            ExternArgs::Stable => write!(f, "STABLE"),
            ExternArgs::Volatile => write!(f, "VOLATILE"),
            ExternArgs::Raw => Ok(()),
            ExternArgs::ParallelSafe => write!(f, "PARALLEL SAFE"),
            ExternArgs::ParallelUnsafe => write!(f, "PARALLEL UNSAFE"),
            ExternArgs::SecurityDefiner => write!(f, "SECURITY DEFINER"),
            ExternArgs::SecurityInvoker => write!(f, "SECURITY INVOKER"),
            ExternArgs::ParallelRestricted => write!(f, "PARALLEL RESTRICTED"),
            ExternArgs::ShouldPanic(_) => Ok(()),
            ExternArgs::NoGuard => Ok(()),
            ExternArgs::Schema(_) => Ok(()),
            ExternArgs::Name(_) => Ok(()),
            ExternArgs::Cost(cost) => write!(f, "COST {cost}"),
            ExternArgs::Requires(_) => Ok(()),
        }
    }
}

impl ToTokens for ExternArgs {
    fn to_tokens(&self, tokens: &mut TokenStream) {
        match self {
            ExternArgs::CreateOrReplace => tokens.append(format_ident!("CreateOrReplace")),
            ExternArgs::Immutable => tokens.append(format_ident!("Immutable")),
            ExternArgs::Strict => tokens.append(format_ident!("Strict")),
            ExternArgs::Stable => tokens.append(format_ident!("Stable")),
            ExternArgs::Volatile => tokens.append(format_ident!("Volatile")),
            ExternArgs::Raw => tokens.append(format_ident!("Raw")),
            ExternArgs::NoGuard => tokens.append(format_ident!("NoGuard")),
            ExternArgs::SecurityDefiner => tokens.append(format_ident!("SecurityDefiner")),
            ExternArgs::SecurityInvoker => tokens.append(format_ident!("SecurityInvoker")),
            ExternArgs::ParallelSafe => tokens.append(format_ident!("ParallelSafe")),
            ExternArgs::ParallelUnsafe => tokens.append(format_ident!("ParallelUnsafe")),
            ExternArgs::ParallelRestricted => tokens.append(format_ident!("ParallelRestricted")),
            ExternArgs::ShouldPanic(_s) => {
                tokens.append_all(
                    quote! {
                        Error(String::from("#_s"))
                    }
                    .to_token_stream(),
                );
            }
            ExternArgs::Schema(_s) => {
                tokens.append_all(
                    quote! {
                        Schema(String::from("#_s"))
                    }
                    .to_token_stream(),
                );
            }
            ExternArgs::Name(_s) => {
                tokens.append_all(
                    quote! {
                        Name(String::from("#_s"))
                    }
                    .to_token_stream(),
                );
            }
            ExternArgs::Cost(_s) => {
                tokens.append_all(
                    quote! {
                        Cost(String::from("#_s"))
                    }
                    .to_token_stream(),
                );
            }
            ExternArgs::Requires(items) => {
                tokens.append_all(
                    quote! {
                        Requires(vec![#(#items),*])
                    }
                    .to_token_stream(),
                );
            }
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

    use crate::{parse_extern_attributes, ExternArgs};

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
