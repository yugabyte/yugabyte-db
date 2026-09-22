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

Positioning references for Rust to SQL mapping support.

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use quote::{ToTokens, quote};
use std::fmt::Display;
use syn::parse::{Parse, ParseStream};

#[derive(Debug, Clone, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub enum PositioningRef {
    FullPath(String),
    Name(String),
}

impl Display for PositioningRef {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::FullPath(i) => f.write_str(i),
            Self::Name(i) => f.write_str(i),
        }
    }
}

impl Parse for PositioningRef {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        let maybe_litstr: Option<syn::LitStr> = input.parse()?;
        let found = if let Some(litstr) = maybe_litstr {
            Self::Name(litstr.value())
        } else {
            let path: syn::Path = input.parse()?;
            let path_str = path.to_token_stream().to_string().replace(' ', "");
            Self::FullPath(path_str)
        };
        Ok(found)
    }
}

impl PositioningRef {
    pub fn section_len_tokens(&self) -> proc_macro2::TokenStream {
        match self {
            Self::FullPath(item) | Self::Name(item) => quote! {
                ::pgrx::pgrx_sql_entity_graph::section::u8_len()
                    + ::pgrx::pgrx_sql_entity_graph::section::str_len(#item)
            },
        }
    }

    pub fn section_writer_tokens(
        &self,
        writer: proc_macro2::TokenStream,
    ) -> proc_macro2::TokenStream {
        match self {
            Self::FullPath(item) => quote! {
                #writer
                    .u8(::pgrx::pgrx_sql_entity_graph::section::POSITIONING_REF_FULL_PATH)
                    .str(#item)
            },
            Self::Name(item) => quote! {
                #writer
                    .u8(::pgrx::pgrx_sql_entity_graph::section::POSITIONING_REF_NAME)
                    .str(#item)
            },
        }
    }
}

impl ToTokens for PositioningRef {
    fn to_tokens(&self, tokens: &mut proc_macro2::TokenStream) {
        let toks = match self {
            Self::FullPath(item) => quote! {
                ::pgrx::pgrx_sql_entity_graph::PositioningRef::FullPath(String::from(#item))
            },
            Self::Name(item) => quote! {
                ::pgrx::pgrx_sql_entity_graph::PositioningRef::Name(String::from(#item))
            },
        };
        toks.to_tokens(tokens);
    }
}
