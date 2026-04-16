# Varlena Types

Conceptually, any "varlena" type is:
```rust
#[repr(C, packed)]
union varlena {
    packed: (u8, [u8]),
    full: (u32, [u8]),
}
```

Unfortunately, Rust does not take kindly to `?Sized` unions, and it would be a bit dodgy in C, too.

<!-- TODO: Explain the whole mess around ?Sized lol -->
