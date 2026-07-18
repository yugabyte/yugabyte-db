# Safety of `pgrx`

Documentation for invariants that `pgrx` relies on for the soundness of its Rust interface,
or ways that `pgrx` compensates for assumed non-invariants, or just notes about
the quirks of Postgres that have been discovered.

Specific functions will have their safety conditions documented on them,
so this document is only useful for describing higher-level concepts.

## Postgres

Quirks specific to Postgres.

### Memory Allocation

The `palloc*` family of functions may throw a Postgres error but will not return `nullptr`.

## Rust

Quirks specific to Rust that specifically inform the design of this crate and not, say,
"every single crate ever".

### Destructors Are Not Guaranteed And `sig{set,long}jmp` Is Weird

Rust does not guarantee that a `Drop::drop` implementation, even if it is described, will actually
be run, due to the ways control flow can be interrupted before the destructor starts or finishes.
Indeed, Drop implementations can be precisely a source of such problems if they are "non-trivial".
Rust control flow has to be independently safe from Postgres control flow to keep Postgres safe from Rust,
and Rust safe from Postgres.

Accordingly, it should be noted that Rust isn't really designed with `sigsetjmp` or `siglongjmp` in mind,
even though they are used in this crate and work well enough at making Rust more manageable
in the face of the various machinations that Postgres may get up to.
