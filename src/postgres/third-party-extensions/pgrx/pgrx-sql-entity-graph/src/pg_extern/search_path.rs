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

`#[pg_extern]` search path related macro expansion for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use proc_macro2::TokenStream as TokenStream2;
use quote::{quote, ToTokens};
use syn::parse::{Parse, ParseStream};
use syn::punctuated::Punctuated;
use syn::Token;

#[derive(Debug, Clone)]
pub struct SearchPath {
    at_start: Option<syn::token::At>,
    dollar: Option<syn::token::Dollar>,
    path: syn::Ident,
    at_end: Option<syn::token::At>,
}

impl Parse for SearchPath {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        Ok(Self {
            at_start: input.parse()?,
            dollar: input.parse()?,
            path: input.parse()?,
            at_end: input.parse()?,
        })
    }
}

impl ToTokens for SearchPath {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let at_start = self.at_start;
        let dollar = self.dollar;
        let path = &self.path;
        let at_end = self.at_end;

        let quoted = quote! {
            concat!(stringify!(#at_start), stringify!(#dollar), stringify!(#path), stringify!(#at_end))
        };

        quoted.to_tokens(tokens);
    }
}

#[derive(Debug, Clone)]
pub struct SearchPathList {
    fields: Punctuated<SearchPath, Token![,]>,
}

impl Parse for SearchPathList {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        Ok(Self { fields: input.parse_terminated(SearchPath::parse, Token![,])? })
    }
}

impl ToTokens for SearchPathList {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        self.fields.to_tokens(tokens)
    }
}
