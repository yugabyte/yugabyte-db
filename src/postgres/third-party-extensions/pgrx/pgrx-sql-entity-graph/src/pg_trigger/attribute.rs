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

`#[pg_trigger]` attribute related macro expansion for Rust to SQL translation

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use crate::ToSqlConfig;

use syn::parse::{Parse, ParseStream};
use syn::Token;

#[derive(Debug, Clone, Hash, Eq, PartialEq)]
pub enum PgTriggerAttribute {
    Sql(ToSqlConfig),
}

impl Parse for PgTriggerAttribute {
    fn parse(input: ParseStream) -> Result<Self, syn::Error> {
        let ident: syn::Ident = input.parse()?;
        let found = match ident.to_string().as_str() {
            "sql" => {
                use crate::pgrx_attribute::ArgValue;
                use syn::Lit;

                let _eq: Token![=] = input.parse()?;
                match input.parse::<ArgValue>()? {
                    ArgValue::Path(p) => Self::Sql(ToSqlConfig::from(p)),
                    ArgValue::Lit(Lit::Bool(b)) => Self::Sql(ToSqlConfig::from(b.value)),
                    ArgValue::Lit(Lit::Str(s)) => Self::Sql(ToSqlConfig::from(s)),
                    ArgValue::Lit(other) => {
                        return Err(syn::Error::new(
                            other.span(),
                            "expected boolean, path, or string literal",
                        ))
                    }
                }
            }
            e => {
                return Err(syn::Error::new(
                    // FIXME: add a UI test for this
                    input.span(),
                    format!("Invalid option `{e}` inside `{ident} {input}`"),
                ));
            }
        };
        Ok(found)
    }
}
