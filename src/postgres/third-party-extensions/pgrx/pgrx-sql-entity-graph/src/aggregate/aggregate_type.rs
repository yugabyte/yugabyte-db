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

`#[pg_aggregate]` related type metadata for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use super::get_pgrx_attr_macro;
use crate::pg_extern::NameMacro;
use crate::UsedType;

use proc_macro2::TokenStream as TokenStream2;
use quote::ToTokens;
use syn::parse::{Parse, ParseStream};
use syn::{parse_quote, Expr, Type};

#[derive(Debug, Clone)]
pub struct AggregateTypeList {
    pub found: Vec<AggregateType>,
    pub original: syn::Type,
}

impl AggregateTypeList {
    pub fn new(maybe_type_list: syn::Type) -> Result<Self, syn::Error> {
        match &maybe_type_list {
            Type::Tuple(tuple) => {
                let mut coll = Vec::new();
                for elem in &tuple.elems {
                    let parsed_elem = AggregateType::new(elem.clone())?;
                    coll.push(parsed_elem);
                }
                Ok(Self { found: coll, original: maybe_type_list })
            }
            ty => {
                Ok(Self { found: vec![AggregateType::new(ty.clone())?], original: maybe_type_list })
            }
        }
    }

    pub fn entity_tokens(&self) -> Expr {
        let found = self.found.iter().map(|x| x.entity_tokens());
        parse_quote! {
            vec![#(#found),*]
        }
    }
}

impl Parse for AggregateTypeList {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        Self::new(input.parse()?)
    }
}

impl ToTokens for AggregateTypeList {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        self.original.to_tokens(tokens)
    }
}

#[derive(Debug, Clone)]
pub struct AggregateType {
    pub used_ty: UsedType,
    /// The name, if it exists.
    pub name: Option<String>,
}

impl AggregateType {
    pub fn new(ty: syn::Type) -> Result<Self, syn::Error> {
        let (name_macro, name) = if let Some(name_macro) = get_pgrx_attr_macro("name", &ty) {
            let name_macro = syn::parse2::<NameMacro>(name_macro)?;
            let name = Some(name_macro.ident.clone());
            (Some(name_macro), name)
        } else {
            (None, None)
        };

        let used_ty = name_macro.map(|v| v.used_ty).unwrap_or(UsedType::new(ty)?);

        let retval = Self { used_ty, name };
        Ok(retval)
    }

    pub fn entity_tokens(&self) -> Expr {
        let used_ty_entity_tokens = self.used_ty.entity_tokens();
        let name = self.name.iter();
        parse_quote! {
            ::pgrx::pgrx_sql_entity_graph::AggregateTypeEntity {
                used_ty: #used_ty_entity_tokens,
                name: None #( .unwrap_or(Some(#name)) )*,
            }
        }
    }
}

impl ToTokens for AggregateType {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        self.used_ty.resolved_ty.to_tokens(tokens)
    }
}

impl Parse for AggregateType {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        Self::new(input.parse()?)
    }
}

#[cfg(test)]
mod tests {
    use super::AggregateTypeList;
    use eyre::{eyre as eyre_err, Result};
    use syn::parse_quote;

    #[test]
    fn solo() -> Result<()> {
        let tokens: syn::Type = parse_quote! {
            i32
        };
        // It should not error, as it's valid.
        let list = AggregateTypeList::new(tokens);
        assert!(list.is_ok());
        let list = list.unwrap();
        let found = &list.found[0];
        let found_string = match &found.used_ty.resolved_ty {
            syn::Type::Path(ty_path) => ty_path.path.segments.last().unwrap().ident.to_string(),
            _ => return Err(eyre_err!("Wrong found.used_ty.resolved_ty")),
        };
        assert_eq!(found_string, "i32");
        Ok(())
    }

    #[test]
    fn list() -> Result<()> {
        let tokens: syn::Type = parse_quote! {
            (i32, i8)
        };
        // It should not error, as it's valid.
        let list = AggregateTypeList::new(tokens);
        assert!(list.is_ok());
        let list = list.unwrap();
        let first = &list.found[0];
        let first_string = match &first.used_ty.resolved_ty {
            syn::Type::Path(ty_path) => ty_path.path.segments.last().unwrap().ident.to_string(),
            _ => return Err(eyre_err!("Wrong first.used_ty.resolved_ty: {:?}", first)),
        };
        assert_eq!(first_string, "i32");

        let second = &list.found[1];
        let second_string = match &second.used_ty.resolved_ty {
            syn::Type::Path(ty_path) => ty_path.path.segments.last().unwrap().ident.to_string(),
            _ => return Err(eyre_err!("Wrong second.used_ty.resolved_ty: {:?}", second)),
        };
        assert_eq!(second_string, "i8");
        Ok(())
    }
}
