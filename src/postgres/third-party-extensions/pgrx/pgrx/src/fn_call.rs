//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.

use pgrx_pg_sys::errcodes::PgSqlErrorCode;
use pgrx_pg_sys::ffi::pg_guard_ffi_boundary;
use pgrx_pg_sys::PgTryBuilder;
use std::panic::AssertUnwindSafe;

use crate::memcx;
use crate::pg_catalog::pg_proc::{PgProc, ProArgMode, ProKind};
use crate::seal::Sealed;
use crate::{
    direct_function_call, is_a, list::List, pg_sys, pg_sys::AsPgCStr, Array, FromDatum, IntoDatum,
};

/// Augments types that can be used as [`fn_call`] arguments.  This is only implemented for the
/// [`Arg`] enum.
pub unsafe trait FnCallArg: Sealed {
    /// Represent `&self` as a [`pg_sys::Datum`].  This is likely to clone
    fn as_datum(&self, pg_proc: &PgProc, argnum: usize) -> Result<Option<pg_sys::Datum>>;

    /// The Postgres type OID of `&self`
    fn type_oid(&self) -> pg_sys::Oid;
}

/// The kinds of [`fn_call`] arguments.  
pub enum Arg<T> {
    /// The argument value is a SQL NULL
    Null,

    /// The argument's `DEFAULT` value should be used
    Default,

    /// Use this actual value
    Value(T),
}

impl<T> Sealed for Arg<T> {}

unsafe impl<T: IntoDatum + Clone> FnCallArg for Arg<T> {
    fn as_datum(&self, pg_proc: &PgProc, argnum: usize) -> Result<Option<pg_sys::Datum>> {
        match self {
            Arg::Null => Ok(None),
            Arg::Value(v) => Ok(Clone::clone(v).into_datum()),
            Arg::Default => create_default_value(pg_proc, argnum),
        }
    }

    #[inline]
    fn type_oid(&self) -> pg_sys::Oid {
        T::type_oid()
    }
}

/// [`FnCallError`]s represent the set of conditions that could case [`fn_call()`] to fail in a
/// user-recoverable manner.
#[derive(thiserror::Error, Debug, Clone, Eq, PartialEq)]
pub enum FnCallError {
    #[error("Invalid identifier: `{0}`")]
    InvalidIdentifier(String),

    #[error("The specified function does not exist")]
    UndefinedFunction,

    #[error("The specified function exists, but has overloaded versions which are ambiguous given the argument types provided")]
    AmbiguousFunction,

    #[error("Can only dynamically call plain functions")]
    UnsupportedFunctionType,

    #[error("Functions with OUT/IN_OUT/TABLE arguments are not supported")]
    UnsupportedArgumentModes,

    #[error("Functions with argument or return types of `internal` are not supported")]
    InternalTypeNotSupported,

    #[error("The requested return type OID `{0:?}` is not compatible with the actual return type OID `{1:?}`")]
    IncompatibleReturnType(pg_sys::Oid, pg_sys::Oid),

    #[error("Function call has more arguments than are supported")]
    TooManyArguments,

    #[error("Did not provide enough non-default arguments")]
    NotEnoughArguments,

    #[error("Function has no default arguments")]
    NoDefaultArguments,

    #[error("Argument #{0} does not have a DEFAULT value")]
    NotDefaultArgument(usize),

    #[error("Argument's default value is not a constant expression")]
    DefaultNotConstantExpression,
}

pub type Result<T> = std::result::Result<T, FnCallError>;

/// Dynamically call a named function in the current database.  The function must be one previously
/// defined by `CREATE FUNCTION`.  Its underlying `LANGUAGE` is irrelevant -- call `LANGUAGE sql`,
/// `LANGUAGE plpgsql`, `LANGUAGE plperl`, or (our favorite) `LANGUAGE plrust` functions.
///
/// The function name itself, `fname`, is an optionally schema-qualified function name identifier.
/// If `fname` is not schema qualified, then the standard Postgres rules for searching the active
/// `SEARCH_PATH` are followed.
///
/// When resolving a function by name, its argument count and types must also be considered.  These
/// are determined by the types applied to the [`Arg`] variants of each provided argument.  To
/// help avoid ambiguities during function resolution, even the [`Arg::Default`] and [`Arg::Null`]
/// variants are typed -- [`Arg`] itself is generic.
///
/// If the called function is declared `STRICT` and at least one of the `args` are the [`Arg::Null`]
/// variant, then the function is **not** actually called, and an `Ok(None)` is returned.
///
/// Arguments with `DEFAULT` clauses always appear as the last function arguments.  If you want the
/// default value as defined by the function, either use [`Arg::Default`] or elide the default
/// arguments entirely -- [`fn_call`] will automatically fill them in.
///
/// # Returns
///
/// [`fn_call`] returns an instantiated Rust type of the return value from the called function.  If
/// that function returns NULL, [`fn_call`] returns `Ok(None)`.
///
/// # Errors
///
/// Any of the [`FnCallError`] variants could be returned.
///
/// # Panics
///
/// [`fn_call`] itself should not panic, but it may raise assertion panics if unexpected conditions are
/// detected.  These would indicate a bug and should be reported.
///
/// Note that if the function being called raises a Postgres ERROR, it is not caught by [`fn_call`]
/// and will immediately abort the transaction.  To catch such errors yourself, use [`fn_call`] with
/// [`PgTryBuilder`].
///
/// # Limitations
///
/// Currently, [`fn_call`] does not support:
///     - Functions that `RETURNS SET OF $type` or `RETURNS TABLE(...)`
///     - Functions with `IN_OUT` or `OUT` arguments
///
/// # Examples
///
/// ## Calling a UDF
///
/// ```sql
/// CREATE FUNCTION sum_array(a int4[]) RETURNS int4 LANGUAGE sql AS $$ SELECT sum(value) FROM (SELECT unnest($1) value) x $$;
/// ```
///
/// ```rust,no_run
/// use pgrx::fn_call::{Arg, fn_call};
///
/// let array = vec![1,2,3];
/// let sum = fn_call::<i32>("sum_array", &[&Arg::Value(array)]);
/// assert_eq!(sum, Ok(Some(6)));
/// ```
///
/// ## Calling a built-in
///
/// ```rust,no_run
/// use pgrx::fn_call::{Arg, fn_call};
/// let is_eq = fn_call::<bool>("texteq", &[&Arg::Value("hello"), &Arg::Value("world")]);
/// assert!(is_eq == Ok(Some(false)));
/// ```
///
/// ## Using DEFAULT values
///
/// ```sql
/// CREATE FUNCTION mul_by(input bigint, factor bigint DEFAULT 2) RETURNS bigint AS $$ SELECT input * factor $$;
/// ```
///
/// ```rust,no_run
/// use pgrx::fn_call::{Arg, fn_call};
///
/// let product = fn_call::<i64>("mul_by", &[&Arg::Value(42_i64)]);  // uses the default of `2` for `factor`
/// assert_eq!(product, Ok(Some(84)));
///
/// let product = fn_call::<i64>("mul_by", &[&Arg::Value(42_i64), &Arg::<i64>::Default]);  // uses the default of `2` for `factor`
/// assert_eq!(product, Ok(Some(84)));
///
/// let product = fn_call::<i64>("mul_by", &[&Arg::Value(42_i64), &Arg::Value(3_i64)]);  // specifies an explicit value for `factor`
/// assert_eq!(product, Ok(Some(126)));
///
/// ```
pub fn fn_call<R: FromDatum + IntoDatum>(
    fname: &str,
    args: &[&dyn FnCallArg],
) -> Result<Option<R>> {
    fn_call_with_collation(fname, pg_sys::DEFAULT_COLLATION_OID, args)
}

pub fn fn_call_with_collation<R: FromDatum + IntoDatum>(
    fname: &str,
    collation: pg_sys::Oid,
    args: &[&dyn FnCallArg],
) -> Result<Option<R>> {
    // lookup the function by its name
    let func_oid = lookup_fn(fname, args)?;

    // lookup the function's pg_proc entry and do some validation
    let pg_proc = PgProc::new(func_oid).ok_or(FnCallError::UndefinedFunction)?;
    let retoid = pg_proc.prorettype();

    //
    // do some validation to catch the cases we don't/can't directly call
    //

    if !matches!(pg_proc.prokind(), ProKind::Function) {
        // It only makes sense to directly call regular functions.  Calling aggregate or window
        // functions is nonsensical
        return Err(FnCallError::UnsupportedFunctionType);
    } else if pg_proc.proargmodes().iter().any(|mode| *mode != ProArgMode::In) {
        // Right now we only know how to support arguments with the IN mode.  Perhaps in the
        // future we can support IN_OUT and TABLE return types
        return Err(FnCallError::UnsupportedArgumentModes);
    } else if retoid == pg_sys::INTERNALOID
        || pg_proc.proargtypes().iter().any(|oid| *oid == pg_sys::INTERNALOID)
    {
        // No idea what to do with the INTERNAL type.  Generally it's just a raw pointer but pgrx
        // has no way to express that with `IntoDatum`.  And passing around raw pointers seem
        // unsafe enough that if someone needs to do that, they probably have the ability to
        // re-implement this function themselves.
        return Err(FnCallError::InternalTypeNotSupported);
    } else if !R::is_compatible_with(retoid) {
        // the requested Oid of `T` is not compatible with the actual function return type
        return Err(FnCallError::IncompatibleReturnType(R::type_oid(), retoid));
    }

    // we're likely going to be able to call the function, so convert our arguments into Datums,
    // filling in any DEFAULT arguments at the end
    let mut null = false;
    let arg_datums = args
        .iter()
        .enumerate()
        .map(|(i, a)| a.as_datum(&pg_proc, i))
        .chain((args.len()..pg_proc.pronargs()).map(|i| create_default_value(&pg_proc, i)))
        .map(|datum| {
            null |= matches!(datum, Ok(None));
            datum
        })
        .collect::<Result<Vec<_>>>()?;
    let nargs = arg_datums.len();

    // if the function is STRICT and at least one of our argument values is `None` (ie, NULL)...
    // we must return `None` now and not call the function.  Passing a NULL argument to a STRICT
    // function will likely crash Postgres
    let isstrict = pg_proc.proisstrict();
    if null && isstrict {
        return Ok(None);
    }

    //
    // The following code is Postgres-version specific.  Right now, it's compatible with v12+
    // v11 will need a different implementation.
    //
    // NB:  Which I don't want to do since it EOLs in November 2023
    //

    unsafe {
        // construct a stack-allocated `FmgrInfo` instance
        let mut flinfo = pg_sys::FmgrInfo::default();

        // SAFETY:  we just allocated `flinfo`.  Whatever objects `fmgr_info` may allocate
        // are allocated in `CurrentMemoryContext`, which is fine as this `flinfo` doesn't live longer
        // than this stack frame anyway.
        pg_sys::fmgr_info(func_oid, &mut flinfo);

        // heap allocate a `FunctionCallInfoBaseData` properly sized so there's enough room for `nargs` arguments
        //
        // SAFETY: we allocate enough zeroed space for the base FunctionCallInfoBaseData *plus* the number of arguments
        // we have, and we've asserted that we have the correct number of arguments
        assert_eq!(nargs, pg_proc.pronargs());
        let fcinfo = pg_sys::palloc0(
            std::mem::size_of::<pg_sys::FunctionCallInfoBaseData>()
                + std::mem::size_of::<pg_sys::NullableDatum>() * nargs,
        ) as *mut pg_sys::FunctionCallInfoBaseData;

        // initialize it
        // SAFETY: we just palloc'd the `fcinfo` instance so it's de-referencable
        let fcinfo_ref = &mut *fcinfo;
        fcinfo_ref.flinfo = &mut flinfo;
        fcinfo_ref.fncollation = collation;
        fcinfo_ref.context = std::ptr::null_mut();
        fcinfo_ref.resultinfo = std::ptr::null_mut();
        fcinfo_ref.isnull = false;
        fcinfo_ref.nargs = nargs as _;

        // setup the argument array
        // SAFETY:  `fcinfo_ref.args` is the over-allocated space we palloc0'd above.  it's an array
        // of `nargs` `NullableDatum` instances.
        let args_slice = fcinfo_ref.args.as_mut_slice(nargs);
        for (i, datum) in arg_datums.into_iter().enumerate() {
            assert!(!isstrict || datum.is_some()); // no NULL datums if this function is STRICT

            let arg = &mut args_slice[i];
            (arg.value, arg.isnull) =
                datum.map(|d| (d, false)).unwrap_or_else(|| (pg_sys::Datum::from(0), true));
        }

        // call the function
        // SAFETY: `flinfo` was create for us by `fmgr_info` above and `fn_addr` would have been properly set
        let func = *(*fcinfo_ref.flinfo)
            .fn_addr
            .as_ref()
            .expect("function initialization problem: fn_addr not set");

        // SAFETY: `func` is most likely a function pointer on the other side of the Postgres FFI
        // boundary, and we must guard that boundary to ensure any ERRORs will still properly unwind
        // the stack.
        let result_datum = pg_guard_ffi_boundary(|| func(fcinfo));

        // Postgres' "OidFunctionCall" doesn't support returning null, but we can
        let result = R::from_datum(result_datum, fcinfo_ref.isnull);

        // cleanup things we heap allocated
        // SAFETY: we allocated `fcinfo` and we're done with it and nothing we're about to return
        // contains any pointers to it or anything it contains.
        pg_sys::pfree(fcinfo.cast());

        Ok(result)
    }
}

fn lookup_fn(fname: &str, args: &[&dyn FnCallArg]) -> Result<pg_sys::Oid> {
    // ask Postgres to find the function.  This will look for the possibly-qualified named
    // function following the normal SEARCH_PATH rules, ensuring its argument type Oids
    // exactly match the ones from the user's input arguments.  It does not evaluate the
    // return type, so we'll have to do that later
    memcx::current_context(|mcx| {
        let mut parts_list = List::<*mut std::ffi::c_void>::default();
        let result = PgTryBuilder::new(AssertUnwindSafe(|| unsafe {
            let arg_types = args.iter().map(|a| a.type_oid()).collect::<Vec<_>>();
            let nargs: i16 =
                arg_types.len().try_into().map_err(|_| FnCallError::TooManyArguments)?;

            // parse the function name into its possibly-qualified name parts
            let ident_parts = parse_sql_ident(fname)?;
            ident_parts
                .iter_deny_null()
                .map(|part| {
                    // SAFETY:  `.as_pg_cstr()` palloc's a char* and `makeString` just takes ownership of it
                    pg_sys::makeString(part.as_pg_cstr())
                })
                .for_each(|part| {
                    parts_list.unstable_push_in_context(part.cast(), mcx);
                });

            // look up an exact match based on the exact number of arguments we have
            //
            // SAFETY:  we've allocated a PgList with the proper String node elements representing its name
            // and we've allocated Vec of argument type oids which can be represented as a pointer.
            let mut fnoid = pg_sys::LookupFuncName(
                parts_list.as_mut_ptr(),
                nargs as _,
                arg_types.as_ptr(),
                true,
            );

            if fnoid == pg_sys::InvalidOid {
                // if that didn't find a function, maybe we've got some defaults in there, so do a lookup
                // where Postgres will consider that
                fnoid = pg_sys::LookupFuncName(
                    parts_list.as_mut_ptr(),
                    -1,
                    arg_types.as_ptr(),
                    false, // we want the ERROR here -- could be UNDEFINED_FUNCTION or AMBIGUOUS_FUNCTION
                );
            }

            Ok(fnoid)
        }))
        .catch_when(PgSqlErrorCode::ERRCODE_INVALID_PARAMETER_VALUE, |_| {
            Err(FnCallError::InvalidIdentifier(fname.to_string()))
        })
        .catch_when(PgSqlErrorCode::ERRCODE_AMBIGUOUS_FUNCTION, |_| {
            Err(FnCallError::AmbiguousFunction)
        })
        .catch_when(PgSqlErrorCode::ERRCODE_UNDEFINED_FUNCTION, |_| {
            Err(FnCallError::UndefinedFunction)
        })
        .execute();

        unsafe {
            // SAFETY:  we palloc'd the `pg_sys::String` elements of `parts_list` above and so it's
            // safe for us to free them now that they're no longer being used
            parts_list.drain(..).for_each(|s| {
                #[cfg(any(feature = "pg13", feature = "pg14"))]
                {
                    let s = s.cast::<pg_sys::Value>();
                    pg_sys::pfree((*s).val.str_.cast());
                }

                #[cfg(any(feature = "pg15", feature = "pg16", feature = "pg17"))]
                {
                    let s = s.cast::<pg_sys::String>();
                    pg_sys::pfree((*s).sval.cast());
                }
            });
        }
        result
    })
}

/// Parses an arbitrary string as if it is a SQL identifier.  If it's not, [`FnCallError::InvalidIdentifier`]
/// is returned
fn parse_sql_ident(ident: &str) -> Result<Array<&str>> {
    unsafe {
        direct_function_call::<Array<&str>>(
            pg_sys::parse_ident,
            &[ident.into_datum(), true.into_datum()],
        )
        .ok_or_else(|| FnCallError::InvalidIdentifier(ident.to_string()))
    }
}

/// Materializes a the `DEFAULT` value at the specified argument position `argnum` for the specified
/// function `pg_proc`.
///
/// `argnum` is the overall argument position, not the specific argument from just the set of
/// arguments with defaults.  This is noted as Postgres internally understands these as different
/// things.  Given the argument number, [`create_default_value`] determines which argument from the
/// list of DEFAULTed arguments is being requested.
///
/// # Errors
///
/// - [`FnCallError::NotDefaultArgument`] if the specified `argnum` does not have a `DEFAULT` clause
/// - [`FnCallError::DefaultNotConstantExpression`] if the `DEFAULT` clause is one we cannot evaluate
fn create_default_value(pg_proc: &PgProc, argnum: usize) -> Result<Option<pg_sys::Datum>> {
    let non_default_args_cnt = pg_proc.pronargs() - pg_proc.pronargdefaults();
    if argnum < non_default_args_cnt {
        return Err(FnCallError::NotDefaultArgument(argnum));
    }

    let default_argnum = argnum - non_default_args_cnt;
    let node = memcx::current_context(|mcx| {
        let default_value_tree =
            pg_proc.proargdefaults(mcx).ok_or(FnCallError::NoDefaultArguments)?;
        default_value_tree
            .get(default_argnum)
            .ok_or(FnCallError::NotDefaultArgument(argnum))
            .copied()
    })?;

    unsafe {
        // SAFETY:  `arg_root` is okay to be the null pointer here, which indicates we don't care
        // about `eval_const_expressions` providing us extra metrics about what it did/found while
        // evaluating `node`.
        //
        // With that, `node` is a valid Node* taken from the PgProc entry
        let evaluated = pg_sys::eval_const_expressions(std::ptr::null_mut(), node.cast());

        // SAFETY:  evaluated is a valid Node* as that's all `eval_const_expressions` can return
        if is_a(evaluated.cast(), pg_sys::NodeTag::T_Const) {
            let con: *mut pg_sys::Const = evaluated.cast();
            let con_ref = &*con;

            if con_ref.constisnull {
                Ok(None)
            } else {
                Ok(Some(con_ref.constvalue))
            }
        } else {
            // NB:  I am not sure this case could ever happen in the context of a function argument
            // `DEFAULT` clause, but if it does, we should let the caller know.  I don't know what
            // they'd do about it other than instead specifying an explicit value for this argnum
            Err(FnCallError::DefaultNotConstantExpression)
        }
    }
}
