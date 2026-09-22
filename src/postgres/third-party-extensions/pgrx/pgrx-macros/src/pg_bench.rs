//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.

use proc_macro::TokenStream;
use proc_macro2::Ident;
use quote::{format_ident, quote};
use syn::parse::{Parse, ParseStream};
use syn::punctuated::Punctuated;
use syn::spanned::Spanned;
use syn::{Expr, ExprLit, ExprPath, Item, ItemFn, Lit, Token, parse_macro_input};

pub(crate) fn pg_bench(attr: TokenStream, item: TokenStream) -> TokenStream {
    let args = parse_macro_input!(attr as PgBenchArgs);
    let ast = parse_macro_input!(item as syn::Item);

    match ast {
        Item::Fn(func) => expand_pg_bench(func, args).unwrap_or_else(|e| e.into_compile_error()),
        thing => syn::Error::new(thing.span(), "#[pg_bench] can only be applied to functions")
            .into_compile_error(),
    }
    .into()
}

fn expand_pg_bench(func: ItemFn, args: PgBenchArgs) -> syn::Result<proc_macro2::TokenStream> {
    let func_ident = func.sig.ident.clone();
    let run_wrapper_name = format_ident!("__pgrx_bench_run_{}", func_ident);
    let run_wrapper_sql_name = run_wrapper_name.to_string();
    let describe_wrapper_name = format_ident!("__pgrx_bench_describe_{}", func_ident);
    let describe_wrapper_sql_name = describe_wrapper_name.to_string();
    let bench_name = func_ident.to_string();
    let source_line = func.sig.ident.span().start().line as u32;

    let mut run_wrapper_attr = proc_macro2::TokenStream::new();
    run_wrapper_attr.extend(quote! {
        schema = "benches",
        name = #run_wrapper_sql_name
    });
    let mut describe_wrapper_attr = proc_macro2::TokenStream::new();
    describe_wrapper_attr.extend(quote! {
        schema = "benches",
        name = #describe_wrapper_sql_name
    });

    let setup_name = args
        .setup
        .as_ref()
        .map(|path| quote! { Some(stringify!(#path)) })
        .unwrap_or_else(|| quote! { None });
    let setup_fn = args
        .setup
        .as_ref()
        .map(|path| quote! { Some(#path as fn()) })
        .unwrap_or_else(|| quote! { None });
    let transaction_mode = match args.transaction {
        PgBenchTransactionMode::Shared => quote! { ::pgrx_bench::pgrx::TransactionMode::Shared },
        PgBenchTransactionMode::SubtransactionPerBatch => {
            quote! { ::pgrx_bench::pgrx::TransactionMode::SubtransactionPerBatch }
        }
        PgBenchTransactionMode::SubtransactionPerIteration => {
            quote! { ::pgrx_bench::pgrx::TransactionMode::SubtransactionPerIteration }
        }
    };
    let sample_size = args.sample_size;
    let measurement_time_ms = args.measurement_time_ms;
    let warm_up_time_ms = args.warm_up_time_ms;
    let nresamples = args.nresamples;
    let noise_threshold = args.noise_threshold;
    let significance_level = args.significance_level;
    let bench_definition = quote! {
        ::pgrx_bench::pgrx::BenchDefinition {
            schema_name: "benches",
            bench_name: #bench_name,
            function_name: #bench_name,
            setup_function: #setup_name,
            transaction_mode: #transaction_mode,
            source_file: file!(),
            source_line: #source_line,
            config: ::pgrx_bench::pgrx::BenchConfig {
                sample_size: #sample_size,
                measurement_time_ms: #measurement_time_ms,
                warm_up_time_ms: #warm_up_time_ms,
                nresamples: #nresamples,
                noise_threshold: #noise_threshold,
                significance_level: #significance_level,
            },
        }
    };

    Ok(quote! {
        #func

        const _: () = {
            let _signature_guard: for<'a> fn(&'a mut ::pgrx_bench::Bencher<'a>) = #func_ident;
            if !::pgrx_bench::pgrx::module_path_has_benches(module_path!()) {
                panic!("#[pg_bench] can only be used inside #[cfg(feature = \"pg_bench\")] #[pg_schema] mod benches");
            }
        };

        #[::pgrx::pgrx_macros::pg_extern(#run_wrapper_attr)]
        fn #run_wrapper_name(baseline_artifacts: Option<::pgrx::JsonB>) -> ::pgrx::JsonB {
            fn caught_error(error: ::pgrx::pg_sys::panic::CaughtError) -> String {
                match error {
                    ::pgrx::pg_sys::panic::CaughtError::PostgresError(report)
                    | ::pgrx::pg_sys::panic::CaughtError::ErrorReport(report) => {
                        report.message().to_string()
                    }
                    ::pgrx::pg_sys::panic::CaughtError::RustPanic { ereport, .. } => {
                        ereport.message().to_string()
                    }
                }
            }

            // Keep the wrapper-only Postgres runtime glue local to this generated function so the
            // user's benchmark module does not gain extra top-level hidden items.
            struct Runtime;

            impl ::pgrx_bench::pgrx::Runtime for Runtime {
                fn execute_guarded<F, T>(&self, f: F) -> Result<T, String>
                where
                    F: FnOnce() -> Result<T, String>,
                {
                    ::pgrx::PgTryBuilder::new(::std::panic::AssertUnwindSafe(f))
                        .catch_others(|error| Err(caught_error(error)))
                        .catch_rust_panic(|error| Err(caught_error(error)))
                        .execute()
                }

                fn with_subtransaction<F, T>(&self, f: F) -> Result<T, String>
                where
                    F: FnOnce() -> T,
                {
                    let name = ::std::ffi::CString::new("pgrx_bench")
                        .expect("static string should be CString-safe");

                    unsafe {
                        let old_context = ::pgrx::pg_sys::YbCurrentMemoryContext;
                        let old_resource_owner = ::pgrx::pg_sys::CurrentResourceOwner;

                        ::pgrx::pg_sys::BeginInternalSubTransaction(name.as_ptr());

                        let result = ::pgrx::PgTryBuilder::new(::std::panic::AssertUnwindSafe(
                            || Ok::<T, String>(f()),
                        ))
                        .catch_others(|error| Err(caught_error(error)))
                        .catch_rust_panic(|error| Err(caught_error(error)))
                        .execute();

                        match result {
                            Ok(value) => {
                                ::pgrx::pg_sys::ReleaseCurrentSubTransaction();
                                ::pgrx::pg_sys::MemoryContextSwitchTo(old_context);
                                ::pgrx::pg_sys::CurrentResourceOwner = old_resource_owner;
                                Ok(value)
                            }
                            Err(error) => {
                                ::pgrx::pg_sys::MemoryContextSwitchTo(old_context);
                                ::pgrx::pg_sys::RollbackAndReleaseCurrentSubTransaction();
                                ::pgrx::pg_sys::MemoryContextSwitchTo(old_context);
                                ::pgrx::pg_sys::CurrentResourceOwner = old_resource_owner;
                                Err(error)
                            }
                        }
                    }
                }
            }

            let runtime = Runtime;
            ::pgrx::JsonB(::pgrx_bench::pgrx::execute_benchmark(
                #bench_definition,
                #setup_fn,
                #func_ident,
                baseline_artifacts.map(|baseline_artifacts| baseline_artifacts.0),
                &runtime,
            ))
        }

        #[::pgrx::pgrx_macros::pg_extern(#describe_wrapper_attr)]
        fn #describe_wrapper_name() -> ::pgrx::JsonB {
            ::pgrx::JsonB(::pgrx_bench::pgrx::describe_benchmark(#bench_definition))
        }
    })
}

struct PgBenchArgs {
    setup: Option<syn::Path>,
    transaction: PgBenchTransactionMode,
    sample_size: usize,
    measurement_time_ms: u64,
    warm_up_time_ms: u64,
    nresamples: usize,
    noise_threshold: f64,
    significance_level: f64,
}

impl Default for PgBenchArgs {
    fn default() -> Self {
        Self {
            setup: None,
            transaction: PgBenchTransactionMode::Shared,
            sample_size: 100,
            measurement_time_ms: 5_000,
            warm_up_time_ms: 3_000,
            nresamples: 100_000,
            noise_threshold: 0.01,
            significance_level: 0.05,
        }
    }
}

impl Parse for PgBenchArgs {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let mut args = Self::default();

        let items = Punctuated::<PgBenchArg, Token![,]>::parse_terminated(input)?;
        for item in items {
            match item {
                PgBenchArg::Setup(path) => {
                    if args.setup.replace(path).is_some() {
                        return Err(syn::Error::new(
                            input.span(),
                            "duplicate `setup` argument to #[pg_bench]",
                        ));
                    }
                }
                PgBenchArg::Transaction(mode) => args.transaction = mode,
                PgBenchArg::SampleSize(value) => args.sample_size = value,
                PgBenchArg::MeasurementTimeMs(value) => args.measurement_time_ms = value,
                PgBenchArg::WarmUpTimeMs(value) => args.warm_up_time_ms = value,
                PgBenchArg::Nresamples(value) => args.nresamples = value,
                PgBenchArg::NoiseThreshold(value) => args.noise_threshold = value,
                PgBenchArg::SignificanceLevel(value) => args.significance_level = value,
            }
        }

        Ok(args)
    }
}

enum PgBenchArg {
    Setup(syn::Path),
    Transaction(PgBenchTransactionMode),
    SampleSize(usize),
    MeasurementTimeMs(u64),
    WarmUpTimeMs(u64),
    Nresamples(usize),
    NoiseThreshold(f64),
    SignificanceLevel(f64),
}

impl Parse for PgBenchArg {
    fn parse(input: ParseStream) -> syn::Result<Self> {
        let ident: Ident = input.parse()?;
        let ident_str = ident.to_string();
        input.parse::<Token![=]>()?;

        match ident_str.as_str() {
            "setup" => parse_setup_arg(input),
            "transaction" => parse_transaction_arg(input),
            "sample_size" => Ok(Self::SampleSize(parse_lit_usize(input, "sample_size")?)),
            "measurement_time_ms" => {
                Ok(Self::MeasurementTimeMs(parse_lit_u64(input, "measurement_time_ms")?))
            }
            "warm_up_time_ms" => Ok(Self::WarmUpTimeMs(parse_lit_u64(input, "warm_up_time_ms")?)),
            "nresamples" => Ok(Self::Nresamples(parse_lit_usize(input, "nresamples")?)),
            "noise_threshold" => Ok(Self::NoiseThreshold(parse_lit_f64(input, "noise_threshold")?)),
            "significance_level" => {
                Ok(Self::SignificanceLevel(parse_lit_f64(input, "significance_level")?))
            }
            _ => Err(syn::Error::new(
                ident.span(),
                format!("unknown #[pg_bench] argument `{ident_str}`"),
            )),
        }
    }
}

#[derive(Clone, Copy)]
enum PgBenchTransactionMode {
    Shared,
    SubtransactionPerBatch,
    SubtransactionPerIteration,
}

fn parse_setup_arg(input: ParseStream) -> syn::Result<PgBenchArg> {
    let expr: Expr = input.parse()?;
    match expr {
        Expr::Path(ExprPath { path, .. }) => Ok(PgBenchArg::Setup(path)),
        Expr::Lit(ExprLit { lit: Lit::Str(_), .. }) => Err(syn::Error::new(
            expr.span(),
            "`setup` must be a Rust path, for example `setup = prepare_fixture`",
        )),
        other => Err(syn::Error::new(
            other.span(),
            "`setup` must be a Rust path to a zero-argument function",
        )),
    }
}

fn parse_transaction_arg(input: ParseStream) -> syn::Result<PgBenchArg> {
    let literal: syn::LitStr = input.parse()?;
    let mode = match literal.value().as_str() {
        "shared" => PgBenchTransactionMode::Shared,
        "subtransaction_per_batch" => PgBenchTransactionMode::SubtransactionPerBatch,
        "subtransaction_per_iteration" => PgBenchTransactionMode::SubtransactionPerIteration,
        _ => {
            return Err(syn::Error::new(
                literal.span(),
                "transaction must be one of \"shared\", \"subtransaction_per_batch\", or \"subtransaction_per_iteration\"",
            ));
        }
    };

    Ok(PgBenchArg::Transaction(mode))
}

fn parse_lit_usize(input: ParseStream, name: &str) -> syn::Result<usize> {
    let literal: syn::LitInt = input.parse()?;
    literal
        .base10_parse()
        .map_err(|e| syn::Error::new(literal.span(), format!("invalid `{name}` value: {e}")))
}

fn parse_lit_u64(input: ParseStream, name: &str) -> syn::Result<u64> {
    let literal: syn::LitInt = input.parse()?;
    literal
        .base10_parse()
        .map_err(|e| syn::Error::new(literal.span(), format!("invalid `{name}` value: {e}")))
}

fn parse_lit_f64(input: ParseStream, name: &str) -> syn::Result<f64> {
    let literal: syn::LitFloat = input.parse()?;
    literal
        .base10_parse()
        .map_err(|e| syn::Error::new(literal.span(), format!("invalid `{name}` value: {e}")))
}
