# Forging SQL from Rust
<!-- Written 2021-09-20 -->

PostgreSQL offers an extension interface, and it's my belief that Rust is a fantastic language to write extensions for it. [Eric Ridge](https://twitter.com/zombodb) thought so too, and started [`pgrx`](https://github.com/pgcentralfoundation/pgrx/) awhile back. I've been working with him to improve the toolkit, and wanted to share about one of our latest hacks: improving the generation of extension SQL code to interface with Rust.

This post is more on the advanced side, as it assumes knowledge of both Rust and PostgreSQL. We'll approach topics like foreign functions, dynamic linking, procedural macros, and linkers.

<!-- more -->

# Understanding the problem

`pgrx` based PostgreSQL extensions ship as the following:

```bash
$ tree plrust
plrust
├── plrust-1.0.sql
├── libplrust.so
└── plrust.control
```

These [extension objects]((https://www.postgresql.org/docs/current/extend-extensions.html#id-1.8.3.20.11)) include a `*.control` file which the user defines, a `*.so` cdylib that contains the compiled code, and `*.sql` which PostgreSQL loads the extension SQL entities from. We'll talk about generating this SQL today!

These `sql` files must be generated from data within the Rust code. The SQL generator `pgrx` provides needs to:

- Create enums defined by `#[derive(PostgresEnum)]` marked enum.
  ```rust
  #[derive(PostgresEnum, Serialize)]
  pub enum SomeValue {
      One,
      Two,
      Three,
      Four,
      Five,
  } 
  ```
  ```sql
  -- src/generic_enum.rs:8
  -- custom_types::generic_enum::SomeValue
  CREATE TYPE SomeValue AS ENUM (
  	'One',
  	'Two',
  	'Three',
  	'Four',
  	'Five'
  );
  ```
- Create PostgreSQL functions pointing to each `#[pg_extern]` marked function.
  ```rust
  #[pg_extern]
  fn known_animals() -> Animals {
      Animals {
          names: vec!["Sally".into(), "Brandy".into(), "anchovy".into()],
          age_lookup: hashmap! {
              5 => "Sally".into(),
              4 => "Brandy".into(),
              3 => "anchovy".into(),
          },
      }
  }
  ```
  ```sql
  -- src/complex.rs:16
  -- custom_types::complex::known_animals
  CREATE OR REPLACE FUNCTION "known_animals"() RETURNS Animals /* custom_types::complex::Animals */
  STRICT
  LANGUAGE c /* Rust */
  AS 'MODULE_PATHNAME', 'known_animals_wrapper';
  ```
- Create PostgreSQL operators pointing to each `#[pg_operator]` marked function.
  ```rust
  #[pg_operator]
  #[opname(=)]
  fn my_eq(left: MyType, right: MyType) -> bool {
      left == right
  }
  ```
  ```sql
  -- src/lib.rs:15
  -- operators::my_eq
  CREATE OR REPLACE FUNCTION "my_eq"(
  	"left" MyType, /* operators::MyType */
  	"right" MyType /* operators::MyType */
  ) RETURNS bool /* bool */
  STRICT
  LANGUAGE c /* Rust */
  AS 'MODULE_PATHNAME', 'my_eq_wrapper';
  
  -- src/lib.rs:15
  -- operators::my_eq
  CREATE OPERATOR = (
  	PROCEDURE="my_eq",
  	LEFTARG=MyType, /* operators::MyType */
  	RIGHTARG=MyType /* operators::MyType */
  );
  ```
- Create 'Shell types', in & out functions, and 'Base types' for each `#[derive(PostgresType)]` marked type.
  ```rust
  #[derive(PostgresType, Serialize, Deserialize, Debug, Eq, PartialEq)]
  pub struct Animals {
      names: Vec<String>,
      age_lookup: HashMap<i32, String>,
  }
  ```
  ```sql
  -- src/complex.rs:10
  -- custom_types::complex::Animals
  CREATE TYPE Animals;
  
  -- src/complex.rs:10
  -- custom_types::complex::animals_in
  CREATE OR REPLACE FUNCTION "animals_in"(
  	"input" cstring /* &std::ffi::c_str::CStr */
  ) RETURNS Animals /* custom_types::complex::Animals */
  IMMUTABLE PARALLEL SAFE STRICT
  LANGUAGE c /* Rust */
  AS 'MODULE_PATHNAME', 'animals_in_wrapper';
  
  -- src/complex.rs:10
  -- custom_types::complex::animals_out
  CREATE OR REPLACE FUNCTION "animals_out"(
  	"input" Animals /* custom_types::complex::Animals */
  ) RETURNS cstring /* &std::ffi::c_str::CStr */
  IMMUTABLE PARALLEL SAFE STRICT
  LANGUAGE c /* Rust */
  AS 'MODULE_PATHNAME', 'animals_out_wrapper';
  
  -- src/complex.rs:10
  -- custom_types::complex::Animals
  CREATE TYPE Animals (
  	INTERNALLENGTH = variable,
  	INPUT = animals_in, /* custom_types::complex::animals_in */
  	OUTPUT = animals_out, /* custom_types::complex::animals_out */
  	STORAGE = extended
  );
  ```
- Create hash operator families for each `#[derive(PostgresHash)]`:
  ```rust
  #[derive(PostgresEq, PostgresHash,
      Eq, PartialEq, Ord, Hash, PartialOrd,
      PostgresType, Serialize, Deserialize
  )]
  pub struct Thing(String);
  ```
  ```sql
  -- src/derived.rs:20
  -- operators::derived::Thing
  CREATE OPERATOR FAMILY Thing_hash_ops USING hash;
  CREATE OPERATOR CLASS Thing_hash_ops DEFAULT FOR TYPE Thing USING hash FAMILY Thing_hash_ops AS
  	OPERATOR    1   =  (Thing, Thing),
  	FUNCTION    1   Thing_hash(Thing);
  ```
- Create hash operator families for each `#[derive(PostgresOrd)]`:
  ```rust
  #[derive(PostgresEq, PostgresOrd,
      Eq, PartialEq, Ord, Hash, PartialOrd,
      PostgresType, Serialize, Deserialize
  )]
  pub struct Thing(String);
  ```
  ```sql
  -- src/derived.rs:16
  -- operators::derived::Thing
  CREATE OPERATOR FAMILY Thing_btree_ops USING btree;
  CREATE OPERATOR CLASS Thing_btree_ops DEFAULT FOR TYPE Thing USING btree FAMILY Thing_btree_ops AS
  	OPERATOR 1 <,
  	OPERATOR 2 <=,
  	OPERATOR 3 =,
  	OPERATOR 4 >=,
  	OPERATOR 5 >,
  	FUNCTION 1 Thing_cmp(Thing, Thing);
  ```

An earlier version of `cargo-pgrx` had `cargo-pgrx pgrx schema` command that would read your Rust files and generate SQL corresponding to them.

**This worked okay!** But gosh, it's not fun to do that, and there are a lot of complications such as trying to resolve types!

*So, what's more fun than parsing Rust source code and generating SQL?* Parsing Rust code in procedural macros to inject metadata foreign functions, then later creating a binary which re-exports those functions via linker tricks, dynamically loads itself, and calls them all to collect metadata, then builds a dependency graph of them to drive the output!

> Wait... What, that was not your answer? Oh no... Well, bear with me because that's what we're doing.

So, why else should we do this other than fun?

## For more than fun

* **Resolving types is hard to DIY:** Mapping the text of some function definition to some matching SQL type is pretty easy when you are mapping `i32` to `integer`, but it starts to break down when you're mapping things like `Array<Floofer>` to `Floofer[]`. `Array` is from `pgrx::datum::Array`, and we'd need to start reading into `use` statements if we were parsing code... and also what about macros (which may create `#[pg_extern]`s)? *...Oops! We're a compiler!*
* **We can enrich our data:** Instead of scanning code, in proc macros we have opportunities to enrich our data using things like `core::any::TypeId`, or building up accurate Rust to SQL type maps.
* **We don't need to care about dead files:** Mysterious errors might occur if users had Rust files holding conflicting definitions, but one file not being in the source tree.
* **We can handle feature flags and release/debug**: While we can scan and detect features, using the build process to ensure we only work with live code means we get feature flag support, as well as release/debug mode support for free!
* **Better foreign macro interaction:** Scanning source trees doesn't give us the ability to interact safely with other proc macros which might create new `#[pg_extern]` (or similar) definitions.

> Okay, did I convince you? ... No? Dangit. Oh... well, Let's explore anyways!

Here's some fun ideas we pondered (and, in some cases, tried):

* **Create it with `build.rs`:** We'd be right back to code parsing like before! The `build.rs` of a crate is invoked before macro expansion or any type resolution, so same problems, too!
* **Make the macros output fragments to `$OUTDIR`:** We could output metadata, such a JSON files to some `$OUT_DIR` instead, and have `cargo pgrx schema` read them, but that doesn't give us the last pass where we can call `core::any::TypeId`, etc.
* **Use `rust-analyzer` to inspect:** This would work fine, but we couldn't depend on it directly since it's not on [crates.io](https://crates.io/). We'd need to use the command line interface, and the way we thought of seemed reasonable without depending on more external tools.
* **Using [`inventory`](https://github.com/dtolnay/inventory)** we could sprinkle `inventory::submit! { T::new(/* ... */) }` calls around our codebase, and then at runtime call a `inventory::iter::<T>`. 
  + **This worked very well**, but Rust 1.54 re-enabled incremental compilation and broke the functionality. Now, the inventory objects could end up in a different object file and some could be missed. We could 'fix' this by using `codegen-units = 1` but it was not satisfying or ideal.
* **Expose a C function in the library, and call it:** This would totally work except we can't load the extension `.so` without also having the postgres headers around, and that's *... Oof!* We don't really want to make `cargo-pgrx` depend on specific PostgreSQL headers.

**But wait!** It turns out, that can work! We can have the binary re-export the functions and be very careful with what we use!

{{ figure(path="path.jpg", alt="An illuminated path in a forest.", colocated=true, source="https://unsplash.com/photos/sPt5RIjKfpk", photographer="Johannes Plenio") }}

## The (longish) short of it

We're going to produce a binary during the build. We'll have macros output some foreign functions, then the binary will re-export and call them to build up a structure representing our extension. `cargo-pgrx pgrx schema`'s job will be to orcestrate that.

Roughly, we're slipping into the build process this way:

```
                            ┌────────────────────────┐
                            │cargo pgrx discovers     │
                            │__pgrx_internal functions│
                            └───────────────────┬────┘
                                                │
                                                ▼
   Build.rs ─► Macros ─► Compile ─► Link ─► Discovery ─► Generation
                 ▲                   ▲                       ▲
                 │                   │                       │
┌────────────────┴─────┐  ┌──────────┴────────┐ ┌────────────┴────────────┐
│Parse definitions.    │  │Re-export internal │ │Binary receives list,    │
│                      │  │functions to binary│ │dynamically loads itself,│
│Create __pgrx_internal │  │via dynamic-list   │ │creates dependency graph,│
│metadata functions    │  │                   │ │outputs SQL              │
└──────────────────────┘  └───────────────────┘ └─────────────────────────┘
```

During the proc macro expansion process, we can append the [`proc_macro2::TokenStream`](https://docs.rs/proc-macro2/1.0.28/proc_macro2/struct.TokenStream.html) with some metadata functions. For example:

```rust
#[derive(pgrx::PostgresType)]
struct Floof { boof: usize }

// We should extend the TokenStream with something like
#[no_mangle]
pub extern "C" fn __pgrx_internals_type_Floof()
-> pgrx::datum::inventory::SqlGraphEntity {
    todo!()
}
```

How about functions? Same idea:

```rust
#[pg_extern]
pub fn floof_from_boof(boof: usize) -> Floof {
    Floof { boof }
}

// We should extend the TokenStream with something like
#[no_mangle]
pub extern "C" fn __pgrx_internals_fn_floof_from_boof()
-> pgrx::datum::inventory::SqlGraphEntity {
    todo!()
}
```

Then, in our `sql-generator` binary, we need to re-export them! We can do this by setting `linker` in `.cargo/config` to a custom script which includes `dynamic-list`:

```toml
# ...
[target.x86_64-unknown-linux-gnu]
linker = "./.cargo/pgrx-linker-script.sh"
# ...
```

> **Note:** We can't use `rustflags` here since it can't handle environment variables or relative paths.

In `.cargo/pgrx-linker-script.sh`:

```bash
#! /usr/bin/env bash
# Auto-generated by pgrx. You may edit this, or delete it to have a new one created.

if [[ $CARGO_BIN_NAME == "sql-generator" ]]; then
    UNAME=$(uname)
    if [[ $UNAME == "Darwin" ]]; then
	TEMP=$(mktemp pgrx-XXX)
        echo "*_pgrx_internals_*" > ${TEMP}
        gcc -exported_symbols_list ${TEMP} $@
        rm -rf ${TEMP}
    else
        TEMP=$(mktemp pgrx-XXX)
        echo "{ __pgrx_internals_*; };" > ${TEMP}
        gcc -Wl,-dynamic-list=${TEMP} $@
        rm -rf ${TEMP}
    fi
else
    gcc -Wl,-undefined,dynamic_lookup $@
fi

```

We also need to ensure that the `Cargo.toml` has a few relevant settings:

```toml
[lib]
crate-type = ["cdylib", "rlib"]

[profile.dev]
# avoid https://github.com/rust-lang/rust/issues/50007
lto = "thin"

[profile.release]
# avoid https://github.com/rust-lang/rust/issues/50007
lto = "fat"
```

Since these functions all have a particular naming scheme, we can scan for them in `cargo pgrx schema` like so:

```rust
let dsym_path = sql_gen_path.resolve_dsym();
let buffer = ByteView::open(dsym_path.as_deref().unwrap_or(&sql_gen_path))?;
let archive = Archive::parse(&buffer).unwrap();

let mut fns_to_call = Vec::new();
for object in archive.objects() {
    match object {
        Ok(object) => match object.symbols() {
            SymbolIterator::Elf(iter) => {
                for symbol in iter {
                    if let Some(name) = symbol.name {
                        if name.starts_with("__pgrx_internals") {
                            fns_to_call.push(name);
                        }
                    }
                }
            }
            _ => todo!(),
        },
        Err(_e) => {
            todo!();
        }
    }
}
```

This list gets passed into the binary, which builds the entity graph structure using something like:

```rust
let mut entities = Vec::default();
// We *must* use this or the extension might not link in.
let control_file = pgrx_sql_entity_graph::ControlFile:try_from("/the/known/path/to/extname.control")?;
entities.push(SqlGraphEntity::ExtensionRoot(control_file));
unsafe {
    for symbol_to_call in symbols_to_call {
        extern "Rust" {
            fn $symbol_name() -> pgrx_sql_entity_graph::SqlGraphEntity;
        } 
        let entity = unsafe { $symbol_name() };
        entities.push(entity);
    }
};
```

Then the outputs of that get passed to our `PgrxSql` structure, something like:

```rust
let pgrx_sql = PgrxSql::build(
    pgrx::DEFAULT_TYPEID_SQL_MAPPING.clone().into_iter(),         
    pgrx::DEFAULT_SOURCE_ONLY_SQL_MAPPING.clone().into_iter(),
    entities.into_iter()
).unwrap();

tracing::info!(path = %path, "Writing SQL");
pgrx_sql.to_file(path)?;
if let Some(dot_path) = dot {
    tracing::info!(dot = %dot_path, "Writing Graphviz DOT");
    pgrx_sql.to_dot(dot_path)?;
}
```

Since SQL is very order dependent, and Rust is largely not, our SQL generator must build up a dependency graph, we use [`petgraph`](https://docs.rs/petgraph/0.6.0/petgraph/index.html) for this. Once a graph is built, we can topological sort from the root (the control file) and get out an ordered set of SQL entities.

Then, all we have to do is turn them into SQL and write them out!

{{ figure(path="cogs.jpg", alt="Old cogs.", colocated=true, source="https://unsplash.com/photos/hsPFuudRg5I", photographer="Isis França") }}

# Fitting the pieces together

Let's talk more about some of the moving parts. There are a few interacting concepts, but everything starts in a few proc macros.

## Understanding syn/quote

A procedural macro in Rust can slurp up a [`TokenStream`](https://doc.rust-lang.org/proc_macro/struct.TokenStream.html) and output another one. My favorite way to *parse* a token stream is with [`syn`](https://docs.rs/syn/1.0.74/syn/), to output? Well that's [`quote`](https://docs.rs/quote/1.0.9/quote/index.html)!

[`syn::Parse`](https://docs.rs/syn/1.0.74/syn/parse/trait.Parse.html) and [`quote::ToTokens`](https://docs.rs/quote/1.0.9/quote/trait.ToTokens.html) do most of the work here.

[`syn`](https://docs.rs/syn/1.0.74/syn/) contains a large number of predefined structures, such as [`syn::DeriveInput`](https://docs.rs/syn/1.0.74/syn/struct.DeriveInput.html) which can be used too. Often, your structures will be a combination of several of those predefined structures.

You can call [`parse`](https://docs.rs/syn/1.0.74/syn/fn.parse.html), [`parse_str`](https://docs.rs/syn/1.0.74/syn/fn.parse_str.html), or [`parse_quote`](https://docs.rs/syn/1.0.74/syn/macro.parse_quote.html) to create these types:

```rust
let val: syn::LitBool = syn::parse_str(r#"true"#)?;
let val: syn::LitBool = syn::parse_quote!(true);
let val: syn::ItemStruct = syn::parse_quote!(struct Floof;);
```

[`parse_quote`](https://docs.rs/syn/1.0.74/syn/macro.parse_quote.html) works along with [`quote::ToTokens`](https://docs.rs/quote/1.0.9/quote/trait.ToTokens.html). Its use is similar to that of how the [`quote`](https://docs.rs/quote/1.0.9/quote/macro.quote.html) macro works!

```rust
use quote::ToTokens;
let tokens = quote::quote! { struct Floof; };
println!("{}", tokens.to_token_stream());
// Prints:
// struct Floof ;
```

They get called by proc macro declarations:

```rust
#[proc_macro_derive(Example, attributes(demo))]
pub fn example(input: TokenStream) -> TokenStream {
    let parsed = parse_macro_input!(input as ExampleParsed);
    let retval = handle(parsed);
    retval.to_token_stream()
}
```

In the case of custom derives, often something like this works:

```rust
#[proc_macro_derive(Example, attributes(demo))]
pub fn example(input: TokenStream) -> TokenStream {
    let parsed = parse_macro_input!(input as syn::DeriveInput);
    let retval = handle(parsed);
    retval.to_token_stream()
}
```

## Rust's `TypeId`s & Type Names

Rust's standard library is full of treasures, including **[`core::any::TypeId`](https://doc.rust-lang.org/std/any/struct.TypeId.html)** and [`core::any::type_name`](https://doc.rust-lang.org/std/any/fn.type_name.html).

Created via [`TypeId::of<T>()`](https://doc.rust-lang.org/std/any/struct.TypeId.html#method.of), `TypeId`s are unique `TypeId` for any given `T`.
  ```rust
  use core::any::TypeId;
  assert!(TypeId::of<&str>() == TypeId::of<&str>());
  assert!(TypeId::of<&str>() != TypeId::of<usize>());
  assert!(TypeId::of<&str>() != TypeId::of<Option<&str>>());
  ```
  We can use this to determine two types are indeed the same, even if we don't have an instance of the type itself.
  
  During the macro expansion phase, we can write out `TypeId::of<#ty>()` for each used type `pgrx` interacts with (including 'known' types and user types.)
  
  Later in the build phase these calls exist as `TypeId::of<MyType>()`, then during the binary phase, these `TypeId`s get evaluated and registered into a mapping, so they can be queried.

**[`core::any::type_name<T>()`](https://doc.rust-lang.org/std/any/fn.type_name.html)** is a *diagnostic* function available in `core` that makes a 'best-effort' attempt to describe the type.

```rust
assert_eq!(
    core::any::type_name::<Option<String>>(),
    "core::option::Option<alloc::string::String>",
);
```

Unlike `TypeId`s, which result in the same ID at in any part of the code, `type_name` cannot promise this, from the docs:

> The returned string **must not be considered to be a unique identifier** of a type as multiple types may map to the same type name. Similarly, there is **no guarantee that all parts of a type will appear** in the returned string: for example, lifetime specifiers are currently not included. In addition, the **output may change between versions of the compiler**.

So, `type_name` is only somewhat useful, but it's our best tool for inspecting the names of the types we're working with. We can't *depend* on it, but we can use it to infer things, leave human-friendly documentation, or provide our own diagnostics.

```rust
#[derive(pgrx::PostgresType)]
struct Floof { boof: usize }
```

The above gets parsed and new tokens are quasi-quoted atop this template like this:

```rust
#[no_mangle]
pub extern "C" fn  #inventory_fn_name() -> pgrx::datum::inventory::SqlGraphEntity {
    let mut mappings = Default::default();
    <#name #ty_generics as pgrx::datum::WithTypeIds>::register_with_refs(
        &mut mappings,
        stringify!(#name).to_string()
    );
    pgrx::datum::WithSizedTypeIds::<#name #ty_generics>::register_sized_with_refs(
        &mut mappings,
        stringify!(#name).to_string()
    );
    // ...
    let submission = pgrx::inventory::InventoryPostgresType {
        name: stringify!(#name),
        full_path: core::any::type_name::<#name #ty_generics>(),
        mappings,
        // ...
    };
    pgrx::datum::inventory::SqlGraphEntity::Type(submission)
}
```

The proc macro passes will make it into:

```rust
#[no_mangle]
pub extern "C" fn  __pgrx_internals_type_Floof() -> pgrx::datum::inventory::SqlGraphEntity {
    let mut mappings = Default::default();
    <Floof as pgrx::datum::WithTypeIds>::register_with_refs(
        &mut mappings,
        stringify!(Floof).to_string()
    );
    pgrx::datum::WithSizedTypeIds::<Floof>::register_sized_with_refs(
        &mut mappings,
        stringify!(Floof).to_string()
    );
    // ...
    let submission = pgrx::inventory::InventoryPostgresType {
        name: stringify!(Floof),
        full_path: core::any::type_name::<Floof>(),
        mappings,
        // ...
    };
    pgrx::datum::inventory::SqlGraphEntity::Type(submission)
}
```

Next, let's talk about how the `TypeId` mapping is constructed!

## Mapping Rust types to SQL

We can build a `TypeId` mapping of every type `pgrx` itself has builtin support. For example, we could do:

```rust
let mut mapping = HashMap::new();
mapping.insert(TypeId::of<T>(), RustSqlMapping {
    rust: type_name<T>(),
    sql: "MyType",
    id: TypeId::of<T>(),
});
```

This works fine, until we get to extension defined types. They're a bit different!

Since `#[pg_extern]` decorated functions can use not only some custom type `T`, but also other types like `PgBox<T>`, `Option<T>`, `Vec<T>`, or `pgrx::datum::Array<T>` we want to also create mappings for those. So we also need `TypeId`s for those... if they exist.

Types such as `Vec<T>` require a type to be `Sized`, `pgrx::datum::Array<T>` requires a type to implement `IntoDatum`. These are complications, since we can't *always* do something like that unless `MyType` implements those. Unfortunately, Rust doesn't really give us the power in macros to do something like what the [`impls`](https://github.com/nvzqz/impls) crate does in macros, so we can't do something like:

```rust
// This doesn't work
syn::parse_quote! {
    #[no_mangle]
    pub extern "C" fn  #inventory_fn_name() -> pgrx::datum::inventory::SqlGraphEntity {
        let mut mappings = Default::default();
        #hypothetical_if_some_type_impls_sized_block {
            mapping.insert(TypeId::of<Vec<#name>>(), RustSqlMapping {
                rust: core::any::type_name::<#name>(),
                sql: core::any::type_name::<#name>.to_string() + "[]",
                id: TypeId::of<Vec<MyType>>(),
            });
        }
        // ...   
    }
};
```

Thankfully we can use the same strategy as [`impls`](https://github.com/nvzqz/impls#how-it-works):

> Inherent implementations are a higher priority than trait implementations.

First, we'll create a trait, and define a blanket implementation:

```rust
pub trait WithTypeIds {
    /// The [`core::any::TypeId`] of some `T`
    const ITEM_ID: Lazy<TypeId>;
    /// The [`core::any::TypeId`] of some `Vec<T>`, if `T` is sized.
    const VEC_ID: Lazy<Option<TypeId>>;
    // ...
}

impl<T: 'static + ?Sized> WithTypeIds for T {
    const ITEM_ID: Lazy<TypeId> = Lazy::new(|| TypeId::of::<T>());
    const OPTION_ID: Lazy<Option<TypeId>> = Lazy::new(|| None);
    // ...
}
```

This lets us do `<T as WithTypeIds>::ITEM_ID` for any `T`, but the `VEC_ID` won't ever be populated. Next, we'll create a 'wrapper' holding only a `core::marker::PhantomData<T>`

```rust
pub struct WithSizedTypeIds<T>(pub core::marker::PhantomData<T>);

impl<T: 'static> WithSizedTypeIds<T> {
    pub const OPTION_ID: Lazy<Option<TypeId>> = Lazy::new(||
        Some(TypeId::of::<Option<T>>())
    );
    // ...
}
```

Now we can do `WithSizedTypeIds::<T>::VEC_ID` for any `T` to get the `TypeId` for `Vec<T>`, and only get `Some(item)` if that type is indeed sized.

Using this strategy, we can have our `__pgrx_internals` functions build up a mapping of `TypeId`s and what SQL they map to.

## Our pet graph

Once we have a set of SQL entities and a mapping of how different Rust types can be represented in SQL we need to figure out how to order it all.

While this is perfectly fine in Rust:

```rust
struct Dog { floof: Floofiness }

enum Floofiness { Normal, Extra }
```

The same in SQL is not valid.

We use a [`petgraph::stable_graph::StableGraph`](https://docs.rs/petgraph/0.6.0/petgraph/stable_graph/struct.StableGraph.html), inserting all of the SQL entities, then looping back and connecting them all together.

If an extension has something like this:

```rust
#[derive(PostgresType, Serialize, Deserialize, Debug, Eq, PartialEq)]
pub struct Animals {
    // ...
}

#[pg_extern]
fn known_animals() -> Animals {
    todo!()
}
```

We need to go and build an edge reflecting that the function `known_animals` requires the type `Animals` to exist. It also needs edges reflecting that these entities depend on the extension root.

Building up the graph is a two step process, first we populate it with all the `SqlGraphEntity` we found. 

This process involves adding the entity, as well doing things like ensuring the return value has a node in the graph even if it's not defined by the user. Something like `&str`, a builtin value `pgrx` knows how to make into SQL and back.

```rust
fn initialize_externs(
    graph: &mut StableGraph<SqlGraphEntity, SqlGraphRelationship>,
    root: NodeIndex,
    bootstrap: Option<NodeIndex>,
    finalize: Option<NodeIndex>,
    externs: Vec<PgExternEntity>,
    mapped_types: &HashMap<PostgresTypeEntity, NodeIndex>,
    mapped_enums: &HashMap<PostgresEnumEntity, NodeIndex>,
) -> eyre::Result<(
    HashMap<PgExternEntity, NodeIndex>,
    HashMap<String, NodeIndex>,
)> {
    let mut mapped_externs = HashMap::default();
    let mut mapped_builtin_types = HashMap::default();
    for item in externs {
        let entity: SqlGraphEntity = item.clone().into();
        let index = graph.add_node(entity.clone());
        mapped_externs.insert(item.clone(), index);
        build_base_edges(graph, index, root, bootstrap, finalize);

        // ...
        match &item.fn_return {
            PgExternReturnEntity::None | PgExternReturnEntity::Trigger => (),
            PgExternReturnEntity::Iterated(iterated_returns) => {
                for iterated_return in iterated_returns {
                    let mut found = false;
                    for (ty_item, &_ty_index) in mapped_types {
                        if ty_item.id_matches(&iterated_return.0) {
                            found = true;
                            break;
                        }
                    }
                    for (ty_item, &_ty_index) in mapped_enums {
                        if ty_item.id_matches(&iterated_return.0) {
                            found = true;
                            break;
                        }
                    }
                    if !found {
                        mapped_builtin_types
                            .entry(iterated_return.1.to_string())
                            .or_insert_with(|| {
                                graph.add_node(SqlGraphEntity::BuiltinType(
                                    iterated_return.1.to_string(),
                                ))
                            });
                    }
                }
            }
            // ...
        }
    }
    Ok((mapped_externs, mapped_builtin_types))
}
```

Once the graph is fully populated we can circle back and connect rest of the things together! This process includes doing things like connecting the arguments and returns of our `#[pg_extern]` marked functions.

Something like this:

```rust
fn connect_externs(
    graph: &mut StableGraph<SqlGraphEntity, SqlGraphRelationship>,
    externs: &HashMap<PgExternEntity, NodeIndex>,
    schemas: &HashMap<SchemaEntity, NodeIndex>,
    types: &HashMap<PostgresTypeEntity, NodeIndex>,
    enums: &HashMap<PostgresEnumEntity, NodeIndex>,
    builtin_types: &HashMap<String, NodeIndex>,
    extension_sqls: &HashMap<ExtensionSqlEntity, NodeIndex>,
) -> eyre::Result<()> {
    for (item, &index) in externs {
        for (schema_item, &schema_index) in schemas {
            if item.module_path == schema_item.module_path {
                tracing::debug!(
                    from = %item.rust_identifier(),
                    to = %schema_item.rust_identifier(),
                    "Adding Extern after Schema edge"
                );
                graph.add_edge(schema_index, index, SqlGraphRelationship::RequiredBy);
                break;
            }
        }
        
        // ...
        match &item.fn_return {
            PgExternReturnEntity::None | PgExternReturnEntity::Trigger => (),
            PgExternReturnEntity::Iterated(iterated_returns) => {
                for iterated_return in iterated_returns {
                    let mut found = false;
                    for (ty_item, &ty_index) in types {
                        if ty_item.id_matches(&iterated_return.0) {
                            tracing::debug!(
                                from = %item.rust_identifier(),
                                to = %ty_item.rust_identifier(),
                                "Adding Extern after Type (due to return) edge"
                            );
                            graph.add_edge(
                                ty_index,
                                index,
                                SqlGraphRelationship::RequiredByReturn
                            );
                            found = true;
                            break;
                        }
                    }
                    if !found {
                        for (ty_item, &ty_index) in enums {
                            if ty_item.id_matches(&iterated_return.0) {
                                tracing::debug!(
                                    from = %item.rust_identifier(),
                                    to = %ty_item.rust_identifier(),
                                    "Adding Extern after Enum (due to return) edge."
                                );
                                graph.add_edge(
                                    ty_index,
                                    index,
                                    SqlGraphRelationship::RequiredByReturn,
                                );
                                found = true;
                                break;
                            }
                        }
                    }
                    if !found {
                        let builtin_index = builtin_types
                            .get(&iterated_return.1.to_string())
                            .expect(&format!(
                                "Could not fetch Builtin Type {}.",
                                iterated_return.1
                            ));
                        tracing::debug!(
                            from = %item.rust_identifier(),
                            to = iterated_return.1,
                            "Adding Extern after BuiltIn Type (due to return) edge"
                        );
                        graph.add_edge(
                            *builtin_index,
                            index,
                            SqlGraphRelationship::RequiredByReturn,
                        );
                    }
                    // ...
                }
            }
        }
    }
    Ok(())
}
```

With a graph built, we can topologically sort the graph and transform them to SQL representations:


```rust
#[tracing::instrument(level = "error", skip(self))]
pub fn to_sql(&self) -> eyre::Result<String> {
    let mut full_sql = String::new();
    for step_id in petgraph::algo::toposort(&self.graph, None)
        .map_err(|e| eyre_err!("Failed to toposort SQL entities: {e:?}"))?
    {
        let step = &self.graph[step_id];

        let sql = step.to_sql(self)?;

        if !sql.is_empty() {
            full_sql.push_str(&sql);
            full_sql.push('\n');
        }
    }
    Ok(full_sql)
}
```

## Generating the SQL

In order to generate SQL for an entity, define a `ToSql` trait which our `SqlGraphEntity` enum implements like so:

```rust
pub trait ToSql {
    fn to_sql(&self, context: &PgrxSql) -> eyre::Result<String>;
}

/// An entity corresponding to some SQL required by the extension.
#[derive(Debug, Clone, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub enum SqlGraphEntity {
    Schema(InventorySchema),
    Enum(InventoryPostgresEnum),
    // ...
}

impl ToSql for SqlGraphEntity {
    #[tracing::instrument(
        level = "debug",
        skip(self, context),
        fields(identifier = %self.rust_identifier())
    )]
    fn to_sql(&self, context: &super::PgrxSql) -> eyre::Result<String> {
        match self {
            SqlGraphEntity::Schema(item) => {
                if item.name != "public" && item.name != "pg_catalog" {
                    item.to_sql(context)
                } else {
                    Ok(String::default())
                }
            },
            SqlGraphEntity::Enum(item) => item.to_sql(context),
            // ...
        }
    }
}
```

Then the same trait goes on the types which `SqlGraphEntity` wraps, here's what the function looks like for enums:

```rust
impl ToSql for InventoryPostgresEnum {
    #[tracing::instrument(
        level = "debug",
        err,
        skip(self, context),
        fields(identifier = %self.rust_identifier())
    )]
    fn to_sql(&self, context: &super::PgrxSql) -> eyre::Result<String> {
        let self_index = context.enums[self];
        let sql = format!(
            "\n\
                    -- {file}:{line}\n\
                    -- {full_path}\n\
                    CREATE TYPE {schema}{name} AS ENUM (\n\
                        {variants}\
                    );\
                ",
            schema = context.schema_prefix_for(&self_index),
            full_path = self.full_path,
            file = self.file,
            line = self.line,
            name = self.name,
            variants = self
                .variants
                .iter()
                .map(|variant| format!("\t'{variant}'"))
                .collect::<Vec<_>>()
                .join(",\n")
                + "\n",
        );
        tracing::debug!(%sql);
        Ok(sql)
    }
}
```

Other implementations, such as on functions, are a bit more complicated. These functions are tasked with determining, for example, the correct name for an argument in SQL, or the schema which contains the function.


{{ figure(path="clockwork.jpg", alt="Clockwork.", colocated=true, source="https://unsplash.com/photos/u_RiRTA_TtY", photographer="Josh Redd") }}

# Closing thoughts

With the toolkit `pgrx` provides, users of Rust are able to develop PostgreSQL extensions using familiar tooling and workflows. There's still a lot of things we'd love to refine and add to the toolkit, but we think you can start using it, like we do in production at TCDI. We think it's even fun to use.

Thanks to [**@dtolnay**](https://github.com/dtolnay) for making many of the crates discussed here, as well as being such a kind and wise person to exist in the orbit of. Also to [Eric Ridge](https://github.com/eeeebbbbrrrr) for all the PostgreSQL knowledge, and [TCDI](https://www.tcdi.com/) for employing me to work on this exceptionally fun stuff!
