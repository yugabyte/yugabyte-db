//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Helper functions to work with Postgres `varlena *` structures

use crate::{pg_sys, PgBox};
use core::{ops::DerefMut, slice, str};

/// # Safety
///
/// The caller asserts the specified `ptr` really is a non-null, palloc'd [`pg_sys::varlena`] pointer
/// that is aligned to 4 bytes, and that the `len` is a half of [`i32::MAX`]
#[inline(always)]
pub unsafe fn set_varsize_4b(ptr: *mut pg_sys::varlena, len: i32) {
    // #define SET_VARSIZE_4B(PTR,len) \
    // 	(((varattrib_4b *) (PTR))->va_4byte.va_header = (((uint32) (len)) << 2))

    // SAFETY:  A varlena can be safely cast to a varattrib_4b
    let header = &mut (*ptr.cast::<pg_sys::varattrib_4b>()).va_4byte.deref_mut().va_header;
    // Using core::ptr::write(), which never calls drop(), to prevent
    // automatically dropping a field of a ManuallyDrop<T>
    core::ptr::write(header, (len as u32) << 2u32)
}

/// # Safety
///
/// The caller asserts the specified `ptr` really is a non-null, palloc'd [`pg_sys::varlena`] pointer
/// that is aligned to 4 bytes.
#[inline(always)]
#[deprecated(since = "0.12.0", note = "you probably meant set_varsize_4b")]
pub unsafe fn set_varsize(ptr: *mut pg_sys::varlena, len: i32) {
    // #define SET_VARSIZE(PTR, len)				SET_VARSIZE_4B(PTR, len)
    set_varsize_4b(ptr, len)
}

/// # Safety
///
/// The caller asserts the specified `ptr` really is a non-null, palloc'd [`pg_sys::varlena`] pointer
#[inline(always)]
pub unsafe fn set_varsize_1b(ptr: *mut pg_sys::varlena, len: i32) {
    // #define SET_VARSIZE_1B(PTR,len) \
    // 	(((varattrib_1b *) (PTR))->va_header = (((uint8) (len)) << 1) | 0x01)

    // SAFETY:  A varlena can be safely cast to a varattrib_1b
    (*ptr.cast::<pg_sys::varattrib_1b>()).va_header = ((len as u8) << 1) | 0x01
}

/// # Safety
///
/// The caller asserts the specified `ptr` really is a non-null, palloc'd [`pg_sys::varlena`] pointer
#[inline(always)]
pub unsafe fn set_varsize_short(ptr: *mut pg_sys::varlena, len: i32) {
    //    #define SET_VARSIZE_SHORT(PTR, len)			SET_VARSIZE_1B(PTR, len)
    set_varsize_1b(ptr, len)
}

/// ```c
/// #define VARSIZE_EXTERNAL(PTR)                        (VARHDRSZ_EXTERNAL + VARTAG_SIZE(VARTAG_EXTERNAL(PTR)))
/// ```
#[inline]
pub unsafe fn varsize_external(ptr: *const pg_sys::varlena) -> usize {
    pg_sys::VARHDRSZ_EXTERNAL + vartag_size(vartag_external(ptr) as pg_sys::vartag_external::Type)
}

/// ```c
/// #define VARTAG_EXTERNAL(PTR)                        VARTAG_1B_E(PTR)
/// ```
#[inline]
pub unsafe fn vartag_external(ptr: *const pg_sys::varlena) -> u8 {
    vartag_1b_e(ptr)
}

/// ```c
/// #define VARTAG_IS_EXPANDED(tag) \
///      (((tag) & ~1) == VARTAG_EXPANDED_RO)
/// ```
#[inline]
pub unsafe fn vartag_is_expanded(tag: pg_sys::vartag_external::Type) -> bool {
    (tag & !1) == pg_sys::vartag_external::VARTAG_EXPANDED_RO
}

/// ```c
/// #define VARTAG_SIZE(tag) \
///      ((tag) == VARTAG_INDIRECT ? sizeof(varatt_indirect) : \
///       VARTAG_IS_EXPANDED(tag) ? sizeof(varatt_expanded) : \
///       (tag) == VARTAG_ONDISK ? sizeof(varatt_external) : \
///       TrapMacro(true, "unrecognized TOAST vartag"))
/// ```
#[inline]
pub unsafe fn vartag_size(tag: pg_sys::vartag_external::Type) -> usize {
    if tag == pg_sys::vartag_external::VARTAG_INDIRECT {
        std::mem::size_of::<pg_sys::varatt_indirect>()
    } else if vartag_is_expanded(tag) {
        std::mem::size_of::<pg_sys::varatt_expanded>()
    } else if tag == pg_sys::vartag_external::VARTAG_ONDISK {
        std::mem::size_of::<pg_sys::varatt_external>()
    } else {
        panic!("unrecognized TOAST vartag")
    }
}

/// ```c
/// #define VARSIZE_4B(PTR) \
/// ((((varattrib_4b *) (PTR))->va_4byte.va_header >> 2) & 0x3FFFFFFF)
/// ```
#[allow(clippy::cast_ptr_alignment)]
#[inline]
pub unsafe fn varsize_4b(ptr: *const pg_sys::varlena) -> usize {
    let va4b = ptr as *const pg_sys::varattrib_4b__bindgen_ty_1; // 4byte
    (((*va4b).va_header >> 2) & 0x3FFF_FFFF) as usize
}

/// ```c
/// #define VARSIZE_1B(PTR) \
/// ((((varattrib_1b *) (PTR))->va_header >> 1) & 0x7F)
/// ```
#[inline]
pub unsafe fn varsize_1b(ptr: *const pg_sys::varlena) -> usize {
    let va1b = ptr as *const pg_sys::varattrib_1b;
    (((*va1b).va_header >> 1) & 0x7F) as usize
}

/// ```c
/// #define VARTAG_1B_E(PTR) \
/// (((varattrib_1b_e *) (PTR))->va_tag)
/// ```
#[inline]
pub unsafe fn vartag_1b_e(ptr: *const pg_sys::varlena) -> u8 {
    let va1be = ptr as *const pg_sys::varattrib_1b_e;
    (*va1be).va_tag
}

#[inline]
pub unsafe fn varsize(ptr: *const pg_sys::varlena) -> usize {
    varsize_4b(ptr)
}

/// ```c
/// #define VARATT_IS_4B(PTR) \
/// ((((varattrib_1b *) (PTR))->va_header & 0x01) == 0x00)
/// ```
#[inline]
pub unsafe fn varatt_is_4b(ptr: *const pg_sys::varlena) -> bool {
    let va1b = ptr as *const pg_sys::varattrib_1b;
    (*va1b).va_header & 0x01 == 0x00
}

/// ```c
/// #define VARATT_IS_4B_U(PTR) \
/// ((((varattrib_1b *) (PTR))->va_header & 0x03) == 0x00)
/// ```
#[allow(clippy::verbose_bit_mask)]
#[inline]
pub unsafe fn varatt_is_4b_u(ptr: *const pg_sys::varlena) -> bool {
    let va1b = ptr as *const pg_sys::varattrib_1b;
    (*va1b).va_header & 0x03 == 0x00
}

/// ```c
/// #define VARATT_IS_4B_C(PTR) \
/// ((((varattrib_1b *) (PTR))->va_header & 0x03) == 0x02)
/// ```
#[inline]
pub unsafe fn varatt_is_4b_c(ptr: *const pg_sys::varlena) -> bool {
    let va1b = ptr as *const pg_sys::varattrib_1b;
    (*va1b).va_header & 0x03 == 0x02
}

/// ```c
/// #define VARATT_IS_1B(PTR) \
/// ((((varattrib_1b *) (PTR))->va_header & 0x01) == 0x01)
/// ```
#[inline]
pub unsafe fn varatt_is_1b(ptr: *const pg_sys::varlena) -> bool {
    let va1b = ptr as *const pg_sys::varattrib_1b;
    (*va1b).va_header & 0x01 == 0x01
}

/// ```c
/// #define VARATT_IS_1B_E(PTR) \
/// ((((varattrib_1b *) (PTR))->va_header) == 0x01)
/// ```
#[inline]
pub unsafe fn varatt_is_1b_e(ptr: *const pg_sys::varlena) -> bool {
    let va1b = ptr as *const pg_sys::varattrib_1b;
    (*va1b).va_header == 0x01
}

/// ```c
/// #define VARATT_NOT_PAD_BYTE(PTR) \
/// (*((uint8 *) (PTR)) != 0)
/// ```
#[inline]
pub unsafe fn varatt_not_pad_byte(ptr: *const pg_sys::varlena) -> bool {
    !ptr.is_null()
}

/// ```c
/// #define VARSIZE_ANY(PTR) \
///      (VARATT_IS_1B_E(PTR) ? VARSIZE_EXTERNAL(PTR) : \
///       (VARATT_IS_1B(PTR) ? VARSIZE_1B(PTR) : \
///        VARSIZE_4B(PTR)))
/// ```
#[inline]
pub unsafe fn varsize_any(ptr: *const pg_sys::varlena) -> usize {
    if varatt_is_1b_e(ptr) {
        varsize_external(ptr)
    } else if varatt_is_1b(ptr) {
        varsize_1b(ptr)
    } else {
        varsize_4b(ptr)
    }
}

/// ```c
/// /* Size of a varlena data, excluding header */
/// #define VARSIZE_ANY_EXHDR(PTR) \
///             (VARATT_IS_1B_E(PTR) ? \
///              VARSIZE_EXTERNAL(PTR)-VARHDRSZ_EXTERNAL : \
///                   ( \
///                  VARATT_IS_1B(PTR) ? \
///                        VARSIZE_1B(PTR)-VARHDRSZ_SHORT : \
///                             VARSIZE_4B(PTR)-VARHDRSZ \
///               ) \
///         )
/// ```
#[inline]
pub unsafe fn varsize_any_exhdr(ptr: *const pg_sys::varlena) -> usize {
    if varatt_is_1b_e(ptr) {
        varsize_external(ptr) - pg_sys::VARHDRSZ_EXTERNAL
    } else if varatt_is_1b(ptr) {
        varsize_1b(ptr) - pg_sys::VARHDRSZ_SHORT
    } else {
        varsize_4b(ptr) - pg_sys::VARHDRSZ
    }
}

/// ```c
/// #define VARDATA_1B(PTR)            (((varattrib_1b *) (PTR))->va_data)
/// ```
#[inline]
pub unsafe fn vardata_1b(ptr: *const pg_sys::varlena) -> *const std::os::raw::c_char {
    let va1b = ptr as *const pg_sys::varattrib_1b;
    (*va1b).va_data.as_slice(varsize_1b(ptr as *const pg_sys::varlena) as usize).as_ptr()
        as *const std::os::raw::c_char
}

/// ```c
/// #define VARDATA_4B(PTR)            (((varattrib_4b *) (PTR))->va_4byte.va_data)
/// ```
#[allow(clippy::cast_ptr_alignment)]
#[inline]
pub unsafe fn vardata_4b(ptr: *const pg_sys::varlena) -> *const std::os::raw::c_char {
    let va1b = ptr as *const pg_sys::varattrib_4b__bindgen_ty_1; // 4byte
    (*va1b).va_data.as_slice(varsize_1b(ptr as *const pg_sys::varlena) as usize).as_ptr()
        as *const std::os::raw::c_char
}

/// ```c
/// #define VARDATA_4B_C(PTR)      (((varattrib_4b *) (PTR))->va_compressed.va_data)
/// ```
#[allow(clippy::cast_ptr_alignment)]
#[inline]
pub unsafe fn vardata_4b_c(ptr: *const pg_sys::varlena) -> *const std::os::raw::c_char {
    let va1b = ptr as *const pg_sys::varattrib_4b__bindgen_ty_2; // compressed
    (*va1b).va_data.as_slice(varsize_1b(ptr as *const pg_sys::varlena) as usize).as_ptr()
        as *const std::os::raw::c_char
}

/// ```c
/// #define VARDATA_1B_E(PTR)      (((varattrib_1b_e *) (PTR))->va_data)
/// ```
#[allow(clippy::cast_ptr_alignment)]
#[inline]
pub unsafe fn vardata_1b_e(ptr: *const pg_sys::varlena) -> *const std::os::raw::c_char {
    let va1b = ptr as *const pg_sys::varattrib_1b_e;
    (*va1b).va_data.as_slice(varsize_1b(ptr as *const pg_sys::varlena) as usize).as_ptr()
        as *const std::os::raw::c_char
}

/// ```c
/// /* caution: this will not work on an external or compressed-in-line Datum */
/// /* caution: this will return a possibly unaligned pointer */
/// #define VARDATA_ANY(PTR) \
///          (VARATT_IS_1B(PTR) ? VARDATA_1B(PTR) : VARDATA_4B(PTR))
/// ```
#[inline]
pub unsafe fn vardata_any(ptr: *const pg_sys::varlena) -> *const std::os::raw::c_char {
    if varatt_is_1b(ptr) {
        vardata_1b(ptr)
    } else {
        vardata_4b(ptr)
    }
}

/// Convert a Postgres `varlena *` (or `text *`) into a Rust `&str`.
///
/// ## Safety
///
/// This function is unsafe because it blindly assumes the provided varlena pointer is non-null.
///
/// Note also that this function is zero-copy and the underlying Rust &str is backed by Postgres-allocated
/// memory.  As such, the return value will become invalid the moment Postgres frees the varlena
#[inline]
pub unsafe fn text_to_rust_str<'a>(
    varlena: *const pg_sys::varlena,
) -> Result<&'a str, str::Utf8Error> {
    let len = varsize_any_exhdr(varlena);
    let data = vardata_any(varlena);

    str::from_utf8(slice::from_raw_parts(data as *mut u8, len))
}

/// Convert a Postgres `varlena *` (or `text *`) into a Rust `&str`.
///
/// ## Safety
///
/// As `text_to_rust_str` but with the additional safety contract that the data is UTF-8.
#[inline]
pub unsafe fn text_to_rust_str_unchecked<'a>(varlena: *const pg_sys::varlena) -> &'a str {
    let len = varsize_any_exhdr(varlena);
    let data = vardata_any(varlena);

    str::from_utf8_unchecked(slice::from_raw_parts(data as *mut u8, len))
}

/// Convert a Postgres `varlena *` (or `byte *`) into a Rust `&[u8]`.
///
/// ## Safety
///
/// This function is unsafe because it blindly assumes the provided varlena pointer is non-null.
///
/// Note also that this function is zero-copy and the underlying Rust `&[u8]` slice is backed by Postgres-allocated
/// memory.  As such, the return value will become invalid the moment Postgres frees the varlena
#[inline]
pub unsafe fn varlena_to_byte_slice<'a>(varlena: *const pg_sys::varlena) -> &'a [u8] {
    let len = varsize_any_exhdr(varlena);
    let data = vardata_any(varlena);

    std::slice::from_raw_parts(data as *const u8, len)
}

/// Convert a Rust `&str` into a Postgres `text *`.
///
/// This allocates the returned Postgres `text *` in `CurrentMemoryContext`.
#[inline]
pub fn rust_str_to_text_p(s: &str) -> PgBox<pg_sys::varlena> {
    let bytea = rust_byte_slice_to_bytea(s.as_bytes());

    // a pg_sys::bytea is a type alias for pg_sys::varlena so this cast is fine
    // SAFETY: bytea will be a valid pointer
    unsafe { PgBox::from_pg(bytea.as_ptr() as *mut pg_sys::varlena) }
}

/// Convert a Rust `&[u8]]` into a Postgres `bytea *` (which is really a varchar)
///
/// This allocates the returned Postgres `bytea *` in `CurrentMemoryContext`.
#[inline]
pub fn rust_byte_slice_to_bytea(slice: &[u8]) -> PgBox<pg_sys::bytea> {
    // SAFETY:  `slice` will provide a valid pointer and pg_sys::cstring_to_text_with_len() will too
    unsafe {
        PgBox::from_pg(pg_sys::cstring_to_text_with_len(
            slice.as_ptr() as *const std::os::raw::c_char,
            slice.len() as i32,
        ))
    }
}
