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

`#[pg_operator]` related macro expansion for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use proc_macro2::TokenStream as TokenStream2;
use quote::{quote, ToTokens, TokenStreamExt};
use syn::parse::{Parse, ParseBuffer};

/// A parsed `#[pg_operator]` operator.
///
/// It is created during [`PgExtern`](crate::PgExtern) parsing.
#[derive(Debug, Default, Clone)]
pub struct PgOperator {
    pub opname: Option<PgrxOperatorOpName>,
    pub commutator: Option<PgrxOperatorAttributeWithIdent>,
    pub negator: Option<PgrxOperatorAttributeWithIdent>,
    pub restrict: Option<PgrxOperatorAttributeWithIdent>,
    pub join: Option<PgrxOperatorAttributeWithIdent>,
    pub hashes: bool,
    pub merges: bool,
}

impl ToTokens for PgOperator {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let opname = self.opname.iter().clone();
        let commutator = self.commutator.iter().clone();
        let negator = self.negator.iter().clone();
        let restrict = self.restrict.iter().clone();
        let join = self.join.iter().clone();
        let hashes = self.hashes;
        let merges = self.merges;
        let quoted = quote! {
            ::pgrx::pgrx_sql_entity_graph::PgOperatorEntity {
                opname: None #( .unwrap_or(Some(#opname)) )*,
                commutator: None #( .unwrap_or(Some(#commutator)) )*,
                negator: None #( .unwrap_or(Some(#negator)) )*,
                restrict: None #( .unwrap_or(Some(#restrict)) )*,
                join: None #( .unwrap_or(Some(#join)) )*,
                hashes: #hashes,
                merges: #merges,
            }
        };
        tokens.append_all(quoted);
    }
}

#[derive(Debug, Clone)]
pub struct PgrxOperatorAttributeWithIdent {
    pub fn_name: TokenStream2,
}

impl Parse for PgrxOperatorAttributeWithIdent {
    fn parse(input: &ParseBuffer) -> Result<Self, syn::Error> {
        Ok(PgrxOperatorAttributeWithIdent { fn_name: input.parse()? })
    }
}

impl ToTokens for PgrxOperatorAttributeWithIdent {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let fn_name = &self.fn_name;
        let operator = fn_name.to_string().replace(' ', "");
        let quoted = quote! {
            #operator
        };
        tokens.append_all(quoted);
    }
}

#[derive(Debug, Clone)]
pub struct PgrxOperatorOpName {
    pub op_name: TokenStream2,
}

impl Parse for PgrxOperatorOpName {
    fn parse(input: &ParseBuffer) -> Result<Self, syn::Error> {
        Ok(PgrxOperatorOpName { op_name: input.parse()? })
    }
}

impl ToTokens for PgrxOperatorOpName {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let op_name = &self.op_name;
        let op_string = op_name.to_string().replacen(' ', "", 256);
        let quoted = quote! {
            #op_string
        };
        tokens.append_all(quoted);
    }
}
