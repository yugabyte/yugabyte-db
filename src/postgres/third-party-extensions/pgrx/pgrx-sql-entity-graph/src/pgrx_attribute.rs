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

`#[pgrx]` attribute for Rust to SQL mapping support.

> Like all of the [`sql_entity_graph`][crate] APIs, this is considered **internal**
> to the `pgrx` framework and very subject to change between versions. While you may use this, please do it with caution.

*/
use syn::parse::{Parse, ParseStream};
use syn::punctuated::Punctuated;
use syn::Token;

/// This struct is intended to represent the contents of the `#[pgrx]` attribute when parsed.
///
/// The intended usage is to parse an `Attribute`, then use `attr.parse_args::<PgrxAttribute>()?` to
/// parse the contents of the attribute into this struct.
///
/// We use this rather than `Attribute::parse_meta` because it is not supported to parse bare paths
/// as values of a `NameValueMeta`, and we want to support that to avoid conflating SQL strings with
/// paths-as-strings. We re-use as much of the standard `parse_meta` structure types as possible though.
pub struct PgrxAttribute {
    pub args: Vec<PgrxArg>,
}

impl Parse for PgrxAttribute {
    fn parse(input: ParseStream<'_>) -> syn::Result<Self> {
        let parser = Punctuated::<PgrxArg, Token![,]>::parse_terminated;
        let punctuated = input.call(parser)?;
        let args = punctuated.into_pairs().map(|p| p.into_value()).collect::<Vec<_>>();
        Ok(Self { args })
    }
}

/// This enum is akin to `syn::Meta`, but supports a custom `NameValue` variant which allows
/// for bare paths in the value position.
#[derive(Debug)]
pub enum PgrxArg {
    NameValue(NameValueArg),
}

impl Parse for PgrxArg {
    /// Parse `name = val` in `#[pgrx(name = val)]`
    ///
    /// It may seem like we leave this unhandled:
    /// ```rust,ignore
    /// #[pg_aggregate]
    /// impl Aggregate for Aggregated {
    ///     #[pgrx(immutable, parallel_safe)]
    ///     fn state(current: _, args: _, fcinfo: _) -> Self::State {
    ///         todo!()
    ///     }
    /// }
    /// ```
    /// However, that actually never reaches this point!
    /// This parser only handles the direct attributes.
    #[track_caller]
    fn parse(input: ParseStream<'_>) -> syn::Result<Self> {
        let path = input.parse::<syn::Path>()?;
        if input.parse::<Token![=]>().is_ok() {
            Ok(Self::NameValue(NameValueArg { path, value: input.parse()? }))
        } else {
            Err(input.error("unsupported argument to #[pgrx] in this context"))
        }
    }
}

/// This struct is akin to `syn::NameValueMeta`, but allows for more than just `syn::Lit` as a value.
#[derive(Debug)]
pub struct NameValueArg {
    pub path: syn::Path,
    pub value: ArgValue,
}

/// This is the type of a value that can be used in the value position of a `name = value` attribute argument.
#[derive(Debug)]
pub enum ArgValue {
    Path(syn::Path),
    Lit(syn::Lit),
}

impl Parse for ArgValue {
    /// Parse `val` in `#[pgrx(name = val)]`
    fn parse(input: ParseStream<'_>) -> syn::Result<Self> {
        if input.peek(syn::Lit) {
            return Ok(Self::Lit(input.parse()?));
        }

        Ok(Self::Path(input.parse()?))
    }
}
