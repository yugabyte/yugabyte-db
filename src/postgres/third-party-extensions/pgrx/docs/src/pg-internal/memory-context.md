# Memory Contexts

Postgres uses a set of "memory contexts" in order to manage memory and prevent leakage, despite
the fact that Postgres code may churn through tables with literally millions of rows. Most of the
memory contexts that an extension's code is likely to be invoked in are transient contexts that
will not outlive the current transaction. These memory contexts will be freed, including all of
their contents, at the end of that transaction. This means that allocations using memory contexts
will quickly be cleaned up, even in C extensions that don't have the power of Rust's compile-time
memory management. However, this is incompatible with certain assumptions Rust makes about safety,
thus making it tricky to correctly bind this code.

Because the memory context's lifetime *also* defines the lifetime of the returned allocation, this
means that any code that returns something allocated in a memory context should be bound by
appropriate lifetime parameters that prevent usage of it beyond that point. As usual, a qualifying
covariant lifetime is often acceptable, meaning any "shorter" lifetime that ends before it is
actually deallocated. The in-progress type to describe this process is `MemCx<'mcx>`, but
understanding how to add to that requires understanding what C does and why it can cause problems
if those idioms are copied to Rust.

## `palloc` and `CurrentMemoryContext`
In extension code, especially that written in C, you may notice calls to the following functions
for allocation and deallocation, instead of the usual `malloc` and `free`:

```c
typedef size_t Size;

extern void *palloc(Size size);
extern void *palloc0(Size size);
extern void *palloc_extended(Size size, int flags);

extern void pfree(void *pointer);
```

<!--
// Only in Postgres 16+
extern void *palloc_aligned(Size size, Size alignto, int flags);
-->

When combined with appropriate type definitions, the `palloc` family of functions are identical to
calling the following functions and passing the `CurrentMemoryContext` as the first argument:

```c
typedef struct MemoryContextData *MemoryContext;
#define PGDLLIMPORT
extern PGDLLIMPORT MemoryContext CurrentMemoryContext;

extern void *MemoryContextAlloc(MemoryContext context, Size size);
extern void *MemoryContextAllocZero(MemoryContext context, Size size);
extern void *MemoryContextAllocExtended(MemoryContext context,
                                        Size size, int flags);
```
<!--
// Only in Postgres 16+
extern void *MemoryContextAllocAligned(MemoryContext context,
                                       Size size, Size alignto, int flags);
-->

Notice that `pfree` only takes the pointer as an argument, effectively meaning every allocation
must know what context it belongs to in some way.

This also means that `palloc` is slightly troublesome. It is actually using global mutable state
to dynamically determine the lifetime of the allocations that are returned, which means that you
may only be able to assign it the lifetime of the current scope! This does not mean it is simply
a menace, however: a common C idiom is creating a new memory context, using it as the global
`CurrentMemoryContext`, then switching back to the previous context and destroying the transient
context, so as to prevent any memory leaks or other objects from escaping. This is both incredibly
useful and also means any concerns about the current context only lasting for the current scope
are actually quite realistic!

## `MemCx<'mcx>`

Fortunately, the behavior of memory contexts is still deterministic and strictly scope-based,
rather than using a dynamic graph of references as a garbage-collected language might. So,
we have a strategy available to make headway on translating ambiguous dynamic lifetimes into a
world of static lifetime constraints:
- Internally prefer the `MemoryContextAlloc` family of functions
- Use the `MemCx<'mcx>` type to "infects" newly allocated types with the lifetime `'mcx`
- Use functions accepting closures like `current_context` to allow obtaining temporary access to
  the desired memory context, while constraining allocated lifetimes to the closure's scope!

### `CurrentMemoryContext` makes `impl Deref` hard

<!-- TODO: this segment. -->

### Assigning lifetimes to `palloc` is hard

<!-- TODO: this segment. -->
