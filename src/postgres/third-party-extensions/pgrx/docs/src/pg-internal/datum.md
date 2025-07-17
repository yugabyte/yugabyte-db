# Pass-By-Datum

The primary way that Postgres passes values between Postgres functions that can hypothetically
have any type is using the "Datum" type. The declaration is written thus in the source code:
```c
typedef uintptr_t Datum;
```

The way Postgres uses Datum is more like a sort of union, which might be logically described as
```rust
#[repr(magic)] // This is not actually ABI-conformant
union Datum {
    bool,
    i8,
    i16,
    i32,
    i64,
    f32,
    f64,
    Oid,
    *mut varlena,
    *mut c_char, // null-terminated cstring
    *mut c_void,
}
```

Thus, sometimes it is a raw pointer, and sometimes it is a value that can fit in a pointer.
This causes it to incur several of the hazards of being a raw pointer, likely to a lifetime-bound
allocation, yet be copied around with the gleeful abandon that one reserves for ordinary bytes.
The only way to determine which variant is in actual usage is to have some other contextual data.

<!-- TODO: finish out the Datum<'_> drafts and provide alternatives to worrying about pointers -->