use crate as pg_sys;
use core::mem::offset_of;
use core::str::FromStr;

/// this comes from `postgres_ext.h`
pub const InvalidOid: crate::Oid = crate::Oid::INVALID;
pub const InvalidOffsetNumber: super::OffsetNumber = 0;
pub const FirstOffsetNumber: super::OffsetNumber = 1;
pub const MaxOffsetNumber: super::OffsetNumber =
    (super::BLCKSZ as usize / std::mem::size_of::<super::ItemIdData>()) as super::OffsetNumber;
pub const InvalidBlockNumber: u32 = 0xFFFF_FFFF as crate::BlockNumber;
pub const VARHDRSZ: usize = std::mem::size_of::<super::int32>();
pub const InvalidCommandId: super::CommandId = (!(0 as super::CommandId)) as super::CommandId;
pub const FirstCommandId: super::CommandId = 0 as super::CommandId;
pub const InvalidTransactionId: crate::TransactionId = crate::TransactionId::INVALID;
pub const BootstrapTransactionId: crate::TransactionId = crate::TransactionId::BOOTSTRAP;
pub const FrozenTransactionId: crate::TransactionId = crate::TransactionId::FROZEN;
pub const FirstNormalTransactionId: crate::TransactionId = crate::TransactionId::FIRST_NORMAL;
pub const MaxTransactionId: crate::TransactionId = crate::TransactionId::MAX;

/// Given a valid HeapTuple pointer, return address of the user data
///
/// # Safety
///
/// This function cannot determine if the `tuple` argument is really a non-null pointer to a [`pg_sys::HeapTuple`].
#[inline(always)]
pub unsafe fn GETSTRUCT(tuple: crate::HeapTuple) -> *mut std::os::raw::c_char {
    // #define GETSTRUCT(TUP) ((char *) ((TUP)->t_data) + (TUP)->t_data->t_hoff)

    // SAFETY:  The caller has asserted `tuple` is a valid HeapTuple and is properly aligned
    // Additionally, t_data.t_hoff is an a u8, so it'll fit inside a usize
    (*tuple).t_data.cast::<std::os::raw::c_char>().add((*(*tuple).t_data).t_hoff as _)
}

//
// TODO: [`TYPEALIGN`] and [`MAXALIGN`] are also part of PR #948 and when that's all merged,
//       their uses should be switched to these
//

#[allow(non_snake_case)]
#[inline(always)]
pub const unsafe fn TYPEALIGN(alignval: usize, len: usize) -> usize {
    // #define TYPEALIGN(ALIGNVAL,LEN)  \
    // (((uintptr_t) (LEN) + ((ALIGNVAL) - 1)) & ~((uintptr_t) ((ALIGNVAL) - 1)))
    ((len) + ((alignval) - 1)) & !((alignval) - 1)
}

#[allow(non_snake_case)]
#[inline(always)]
pub const unsafe fn MAXALIGN(len: usize) -> usize {
    // #define MAXALIGN(LEN) TYPEALIGN(MAXIMUM_ALIGNOF, (LEN))
    TYPEALIGN(pg_sys::MAXIMUM_ALIGNOF as _, len)
}

///  Given a currently-allocated chunk of Postgres allocated memory, determine the context
///  it belongs to.
///
/// All chunks allocated by any memory context manager are required to be
/// preceded by the corresponding MemoryContext stored, without padding, in the
/// preceding sizeof(void*) bytes.  A currently-allocated chunk must contain a
/// backpointer to its owning context.  The backpointer is used by pfree() and
/// repalloc() to find the context to call.
///
/// # Safety
///
/// The specified `pointer` **must** be one allocated by Postgres (via [`palloc`] and friends).
///
///
/// # Panics
///
/// This function will panic if `pointer` is null, if it's not properly aligned, or if the memory
/// it points to doesn't have a prefix that looks like a memory context pointer
///
/// [`palloc`]: crate::palloc
#[allow(non_snake_case)]
pub unsafe fn GetMemoryChunkContext(pointer: *mut std::os::raw::c_void) -> pg_sys::MemoryContext {
    #[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
    {
        // Postgres versions <16 don't export the "GetMemoryChunkContext" function.  It's a "static inline"
        // function in `memutils.h`, so we port it to Rust right here
        /*
         * Try to detect bogus pointers handed to us, poorly though we can.
         * Presumably, a pointer that isn't MAXALIGNED isn't pointing at an
         * allocated chunk.
         */
        assert!(!pointer.is_null());
        assert_eq!(pointer, MAXALIGN(pointer as usize) as *mut ::std::os::raw::c_void);

        /*
         * OK, it's probably safe to look at the context.
         */
        // 	context = *(MemoryContext *) (((char *) pointer) - sizeof(void *));
        let context = unsafe {
            // SAFETY: the caller has assured us that `pointer` points to palloc'd memory, which
            // means it'll have this header before it
            *(pointer
                .cast::<::std::os::raw::c_char>()
                .sub(std::mem::size_of::<*mut ::std::os::raw::c_void>())
                .cast())
        };

        assert!(MemoryContextIsValid(context));

        context
    }
    #[cfg(any(feature = "pg16", feature = "pg17"))]
    {
        #[pgrx_macros::pg_guard]
        extern "C-unwind" {
            #[link_name = "GetMemoryChunkContext"]
            pub fn extern_fn(pointer: *mut std::os::raw::c_void) -> pg_sys::MemoryContext;
        }
        extern_fn(pointer)
    }
}

/// Returns true if memory context is tagged correctly according to Postgres.
///
/// # Safety
///
/// The caller must only attempt this on a pointer to a Node.
/// This may clarify if the pointee is correctly-initialized [`pg_sys::MemoryContextData`].
///
/// # Implementation Note
///
/// If Postgres adds more memory context types in the future, we need to do that here too.
#[allow(non_snake_case)]
#[inline(always)]
pub unsafe fn MemoryContextIsValid(context: crate::MemoryContext) -> bool {
    // #define MemoryContextIsValid(context) \
    // 	((context) != NULL && \
    // 	 (IsA((context), AllocSetContext) || \
    // 	  IsA((context), SlabContext) || \
    // 	  IsA((context), GenerationContext)))

    !context.is_null()
        && unsafe {
            // SAFETY:  we just determined the pointer isn't null, and
            // the caller asserts that it is being used on a Node.
            let tag = (*context.cast::<crate::Node>()).type_;
            use crate::NodeTag::*;
            matches!(tag, T_AllocSetContext | T_SlabContext | T_GenerationContext)
        }
}

pub const VARHDRSZ_EXTERNAL: usize = offset_of!(super::varattrib_1b_e, va_data);
pub const VARHDRSZ_SHORT: usize = offset_of!(super::varattrib_1b, va_data);

#[inline]
pub fn get_pg_major_version_string() -> &'static str {
    super::PG_MAJORVERSION.to_str().unwrap()
}

#[inline]
pub fn get_pg_major_version_num() -> u16 {
    u16::from_str(super::get_pg_major_version_string()).unwrap()
}

#[cfg(any(not(target_env = "msvc"), feature = "pg17"))]
#[inline]
pub fn get_pg_version_string() -> &'static str {
    super::PG_VERSION_STR.to_str().unwrap()
}

#[cfg(all(
    target_env = "msvc",
    any(feature = "pg13", feature = "pg14", feature = "pg15", feature = "pg16")
))]
#[inline]
pub fn get_pg_version_string() -> &'static str {
    // bindgen cannot get value of PG_VERSION_STR
    // PostgreSQL @0@ on @1@-@2@, compiled by @3@-@4@, @5@-bit
    static PG_VERSION_STR: [u8; 256] = const {
        let major = super::PG_MAJORVERSION_NUM;
        let minor = super::PG_MINORVERSION_NUM;
        #[cfg(target_pointer_width = "32")]
        let pointer_width = 32_u32;
        #[cfg(target_pointer_width = "64")]
        let pointer_width = 64_u32;
        // a fake value
        let msc_ver = b"1700";
        let mut buffer = [0u8; 256];
        let mut pointer = 0;
        {
            let s = b"PostgreSQL ";
            let mut i = 0;
            while i < s.len() {
                buffer[pointer + i] = s[i];
                i += 1;
            }
            pointer += s.len();
        }
        {
            buffer[pointer + 0] = b'0' + (major / 10) as u8;
            buffer[pointer + 1] = b'0' + (major % 10) as u8;
            pointer += 2;
        }
        {
            let s = b".";
            let mut i = 0;
            while i < s.len() {
                buffer[pointer + i] = s[i];
                i += 1;
            }
            pointer += s.len();
        }
        if minor < 10 {
            buffer[pointer + 0] = b'0' + (minor % 10) as u8;
            pointer += 1;
        } else {
            buffer[pointer + 0] = b'0' + (minor / 10) as u8;
            buffer[pointer + 1] = b'0' + (minor % 10) as u8;
            pointer += 2;
        }
        {
            let s = b", compiled by Visual C++ build ";
            let mut i = 0;
            while i < s.len() {
                buffer[pointer + i] = s[i];
                i += 1;
            }
            pointer += s.len();
        }
        {
            let s = msc_ver;
            let mut i = 0;
            while i < s.len() {
                buffer[pointer + i] = s[i];
                i += 1;
            }
            pointer += s.len();
        }
        {
            let s = b", ";
            let mut i = 0;
            while i < s.len() {
                buffer[pointer + i] = s[i];
                i += 1;
            }
            pointer += s.len();
        }
        {
            buffer[pointer + 0] = b'0' + (pointer_width / 10) as u8;
            buffer[pointer + 1] = b'0' + (pointer_width % 10) as u8;
            pointer += 2;
        }
        {
            let s = b"-bit";
            let mut i = 0;
            while i < s.len() {
                buffer[pointer + i] = s[i];
                i += 1;
            }
            pointer += s.len();
        }
        buffer[pointer] = 0;
        buffer
    };
    unsafe { std::ffi::CStr::from_ptr(PG_VERSION_STR.as_ptr().cast()).to_str().unwrap() }
}

#[inline]
pub fn get_pg_major_minor_version_string() -> &'static str {
    super::PG_VERSION.to_str().unwrap()
}

#[inline]
pub fn TransactionIdIsNormal(xid: super::TransactionId) -> bool {
    xid >= FirstNormalTransactionId
}

/// ```c
///     #define type_is_array(typid)  (get_element_type(typid) != InvalidOid)
/// ```
#[inline]
pub unsafe fn type_is_array(typoid: super::Oid) -> bool {
    super::get_element_type(typoid) != InvalidOid
}

/// #define BufferGetPage(buffer) ((Page)BufferGetBlock(buffer))
#[inline]
pub unsafe fn BufferGetPage(buffer: crate::Buffer) -> crate::Page {
    BufferGetBlock(buffer) as crate::Page
}

/// #define BufferGetBlock(buffer) \
/// ( \
///      AssertMacro(BufferIsValid(buffer)), \
///      BufferIsLocal(buffer) ? \
///            LocalBufferBlockPointers[-(buffer) - 1] \
///      : \
///            (Block) (BufferBlocks + ((Size) ((buffer) - 1)) * BLCKSZ) \
/// )
#[inline]
pub unsafe fn BufferGetBlock(buffer: crate::Buffer) -> crate::Block {
    if BufferIsLocal(buffer) {
        *crate::LocalBufferBlockPointers.offset(((-buffer) - 1) as isize)
    } else {
        crate::BufferBlocks.add(((buffer as crate::Size) - 1) * crate::BLCKSZ as usize)
            as crate::Block
    }
}

/// #define BufferIsLocal(buffer)      ((buffer) < 0)
#[inline]
pub unsafe fn BufferIsLocal(buffer: crate::Buffer) -> bool {
    buffer < 0
}

/// Retrieve the "user data" of the specified [`pg_sys::HeapTuple`] as a specific type. Typically this
/// will be a struct that represents a Postgres system catalog, such as [`FormData_pg_class`].
///
/// # Returns
///
/// A pointer to the [`pg_sys::HeapTuple`]'s "user data", cast as a mutable pointer to `T`.  If the
/// specified `htup` pointer is null, the null pointer is returned.
///
/// # Safety
///
/// This function cannot verify that the specified `htup` points to a valid [`pg_sys::HeapTuple`] nor
/// that if it does, that its bytes are bitwise compatible with `T`.
///
/// [`FormData_pg_class`]: crate::FormData_pg_class
#[inline]
pub unsafe fn heap_tuple_get_struct<T>(htup: super::HeapTuple) -> *mut T {
    if htup.is_null() {
        std::ptr::null_mut()
    } else {
        unsafe {
            // SAFETY:  The caller has told us `htop` is a valid HeapTuple
            GETSTRUCT(htup).cast()
        }
    }
}

// All of this weird code is in response to Postgres having a relatively cavalier attitude about types:
// - https://github.com/postgres/postgres/commit/1c27d16e6e5c1f463bbe1e9ece88dda811235165
//
// As a result, we redeclare their functions with the arguments they should have on earlier Postgres
// and we route people to the old symbols they were using before on later ones.
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
#[::pgrx_macros::pg_guard]
extern "C-unwind" {
    pub fn planstate_tree_walker(
        planstate: *mut super::PlanState,
        walker: ::core::option::Option<
            unsafe extern "C-unwind" fn(*mut super::PlanState, *mut ::core::ffi::c_void) -> bool,
        >,
        context: *mut ::core::ffi::c_void,
    ) -> bool;

    pub fn query_tree_walker(
        query: *mut super::Query,
        walker: ::core::option::Option<
            unsafe extern "C-unwind" fn(*mut super::Node, *mut ::core::ffi::c_void) -> bool,
        >,
        context: *mut ::core::ffi::c_void,
        flags: ::core::ffi::c_int,
    ) -> bool;

    pub fn query_or_expression_tree_walker(
        node: *mut super::Node,
        walker: ::core::option::Option<
            unsafe extern "C-unwind" fn(*mut super::Node, *mut ::core::ffi::c_void) -> bool,
        >,
        context: *mut ::core::ffi::c_void,
        flags: ::core::ffi::c_int,
    ) -> bool;

    pub fn range_table_entry_walker(
        rte: *mut super::RangeTblEntry,
        walker: ::core::option::Option<
            unsafe extern "C-unwind" fn(*mut super::Node, *mut ::core::ffi::c_void) -> bool,
        >,
        context: *mut ::core::ffi::c_void,
        flags: ::core::ffi::c_int,
    ) -> bool;

    pub fn range_table_walker(
        rtable: *mut super::List,
        walker: ::core::option::Option<
            unsafe extern "C-unwind" fn(*mut super::Node, *mut ::core::ffi::c_void) -> bool,
        >,
        context: *mut ::core::ffi::c_void,
        flags: ::core::ffi::c_int,
    ) -> bool;

    pub fn expression_tree_walker(
        node: *mut super::Node,
        walker: ::core::option::Option<
            unsafe extern "C-unwind" fn(*mut super::Node, *mut ::core::ffi::c_void) -> bool,
        >,
        context: *mut ::core::ffi::c_void,
    ) -> bool;

    pub fn raw_expression_tree_walker(
        node: *mut super::Node,
        walker: ::core::option::Option<
            unsafe extern "C-unwind" fn(*mut super::Node, *mut ::core::ffi::c_void) -> bool,
        >,
        context: *mut ::core::ffi::c_void,
    ) -> bool;
}

#[cfg(any(feature = "pg16", feature = "pg17"))]
pub unsafe fn planstate_tree_walker(
    planstate: *mut super::PlanState,
    walker: ::core::option::Option<
        unsafe extern "C-unwind" fn(*mut super::PlanState, *mut ::core::ffi::c_void) -> bool,
    >,
    context: *mut ::core::ffi::c_void,
) -> bool {
    crate::planstate_tree_walker_impl(planstate, walker, context)
}

#[cfg(any(feature = "pg16", feature = "pg17"))]
pub unsafe fn query_tree_walker(
    query: *mut super::Query,
    walker: ::core::option::Option<
        unsafe extern "C-unwind" fn(*mut super::Node, *mut ::core::ffi::c_void) -> bool,
    >,
    context: *mut ::core::ffi::c_void,
    flags: ::core::ffi::c_int,
) -> bool {
    crate::query_tree_walker_impl(query, walker, context, flags)
}

#[cfg(any(feature = "pg16", feature = "pg17"))]
pub unsafe fn query_or_expression_tree_walker(
    node: *mut super::Node,
    walker: ::core::option::Option<
        unsafe extern "C-unwind" fn(*mut super::Node, *mut ::core::ffi::c_void) -> bool,
    >,
    context: *mut ::core::ffi::c_void,
    flags: ::core::ffi::c_int,
) -> bool {
    crate::query_or_expression_tree_walker_impl(node, walker, context, flags)
}

#[cfg(any(feature = "pg16", feature = "pg17"))]
pub unsafe fn expression_tree_walker(
    node: *mut crate::Node,
    walker: Option<unsafe extern "C-unwind" fn(*mut crate::Node, *mut ::core::ffi::c_void) -> bool>,
    context: *mut ::core::ffi::c_void,
) -> bool {
    crate::expression_tree_walker_impl(node, walker, context)
}

#[cfg(any(feature = "pg16", feature = "pg17"))]
pub unsafe fn range_table_entry_walker(
    rte: *mut super::RangeTblEntry,
    walker: ::core::option::Option<
        unsafe extern "C-unwind" fn(*mut super::Node, *mut ::core::ffi::c_void) -> bool,
    >,
    context: *mut ::core::ffi::c_void,
    flags: ::core::ffi::c_int,
) -> bool {
    crate::range_table_entry_walker_impl(rte, walker, context, flags)
}

#[cfg(any(feature = "pg16", feature = "pg17"))]
pub unsafe fn range_table_walker(
    rtable: *mut super::List,
    walker: ::core::option::Option<
        unsafe extern "C-unwind" fn(*mut super::Node, *mut ::core::ffi::c_void) -> bool,
    >,
    context: *mut ::core::ffi::c_void,
    flags: ::core::ffi::c_int,
) -> bool {
    crate::range_table_walker_impl(rtable, walker, context, flags)
}

#[cfg(any(feature = "pg16", feature = "pg17"))]
pub unsafe fn raw_expression_tree_walker(
    node: *mut crate::Node,
    walker: Option<unsafe extern "C-unwind" fn(*mut crate::Node, *mut ::core::ffi::c_void) -> bool>,
    context: *mut ::core::ffi::c_void,
) -> bool {
    crate::raw_expression_tree_walker_impl(node, walker, context)
}

#[inline(always)]
pub unsafe fn MemoryContextSwitchTo(context: crate::MemoryContext) -> crate::MemoryContext {
    let old = crate::CurrentMemoryContext;

    crate::CurrentMemoryContext = context;
    old
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn BufferGetPageSize(buffer: pg_sys::Buffer) -> pg_sys::Size {
    // #define BufferGetPageSize(buffer) \
    // ( \
    //     AssertMacro(BufferIsValid(buffer)), \
    //     (Size)BLCKSZ \
    // )
    assert!(BufferIsValid(buffer));
    pg_sys::BLCKSZ as pg_sys::Size
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn ItemIdGetOffset(item_id: pg_sys::ItemId) -> u32 {
    // #define ItemIdGetOffset(itemId) \
    // ((itemId)->lp_off)
    (*item_id).lp_off()
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn PageIsValid(page: pg_sys::Page) -> bool {
    // #define PageIsValid(page) PointerIsValid(page)
    !page.is_null()
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn PageIsEmpty(page: pg_sys::Page) -> bool {
    // #define PageIsEmpty(page) \
    // (((PageHeader) (page))->pd_lower <= SizeOfPageHeaderData)
    const SizeOfPageHeaderData: pg_sys::Size =
        core::mem::offset_of!(pg_sys::PageHeaderData, pd_linp);
    let page_header = page as *mut pg_sys::PageHeaderData;
    (*page_header).pd_lower <= SizeOfPageHeaderData as u16
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn PageIsNew(page: pg_sys::Page) -> bool {
    // #define PageIsNew(page) (((PageHeader) (page))->pd_upper == 0)
    let page_header = page as *mut pg_sys::PageHeaderData;
    (*page_header).pd_upper == 0
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn PageGetItemId(page: pg_sys::Page, offset: pg_sys::OffsetNumber) -> pg_sys::ItemId {
    // #define PageGetItemId(page, offsetNumber) \
    // ((ItemId) (&((PageHeader) (page))->pd_linp[(offsetNumber) - 1]))
    let page_header = page as *mut pg_sys::PageHeaderData;
    (*page_header).pd_linp.as_mut_ptr().add(offset as usize - 1)
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn PageGetContents(page: pg_sys::Page) -> *mut ::core::ffi::c_char {
    // #define PageGetContents(page) \
    // ((char *) (page) + MAXALIGN(SizeOfPageHeaderData))
    const SizeOfPageHeaderData: pg_sys::Size =
        core::mem::offset_of!(pg_sys::PageHeaderData, pd_linp);
    page.add(pg_sys::MAXALIGN(SizeOfPageHeaderData) as usize) as *mut ::core::ffi::c_char
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub fn PageSizeIsValid(page_size: usize) -> bool {
    // #define PageSizeIsValid(pageSize) ((pageSize) == BLCKSZ)
    page_size == pg_sys::BLCKSZ as usize
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn PageGetPageSize(page: pg_sys::Page) -> usize {
    // #define PageGetPageSize(page) \
    // ((Size) (((PageHeader) (page))->pd_pagesize_version & (uint16) 0xFF00))
    let page_header = page as *mut pg_sys::PageHeaderData;
    ((*page_header).pd_pagesize_version & 0xFF00) as usize
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn PageGetPageLayoutVersion(page: pg_sys::Page) -> ::core::ffi::c_char {
    // #define PageGetPageLayoutVersion(page) \
    // (((PageHeader) (page))->pd_pagesize_version & 0x00FF)
    let page_header = page as *mut pg_sys::PageHeaderData;
    ((*page_header).pd_pagesize_version & 0x00FF) as ::core::ffi::c_char
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn PageSetPageSizeAndVersion(page: pg_sys::Page, size: u16, version: u8) {
    // #define PageSetPageSizeAndVersion(page, size, version) \
    // ((PageHeader) (page))->pd_pagesize_version = (size) | (version)
    let page_header = page as *mut pg_sys::PageHeaderData;
    (*page_header).pd_pagesize_version = size | (version as u16);
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn PageGetSpecialSize(page: pg_sys::Page) -> u16 {
    // #define PageGetSpecialSize(page) \
    // ((uint16) (PageGetPageSize(page) - ((PageHeader)(page))->pd_special))
    let page_header = page as *mut pg_sys::PageHeaderData;
    PageGetPageSize(page) as u16 - (*page_header).pd_special
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn PageGetSpecialPointer(page: pg_sys::Page) -> *mut ::core::ffi::c_char {
    // #define PageGetSpecialPointer(page) \
    // ((char *) ((char *) (page) + ((PageHeader) (page))->pd_special))
    let page_header = page as *mut pg_sys::PageHeaderData;
    page.add((*page_header).pd_special as usize) as *mut ::core::ffi::c_char
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn PageGetItem(page: pg_sys::Page, item_id: pg_sys::ItemId) -> *mut ::core::ffi::c_char {
    // #define PageGetItem(page, itemId) \
    // (((char *)(page)) + ItemIdGetOffset(itemId))
    page.add(ItemIdGetOffset(item_id) as usize)
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn PageGetMaxOffsetNumber(page: pg_sys::Page) -> pg_sys::OffsetNumber {
    // #define PageGetMaxOffsetNumber(page) \
    // (((PageHeader) (page))->pd_lower <= SizeOfPageHeaderData ? 0 : \
    // ((((PageHeader) (page))->pd_lower - SizeOfPageHeaderData) / sizeof(ItemIdData)))
    const SizeOfPageHeaderData: pg_sys::Size =
        core::mem::offset_of!(pg_sys::PageHeaderData, pd_linp);
    let page_header = page as *mut pg_sys::PageHeaderData;
    if (*page_header).pd_lower <= SizeOfPageHeaderData as u16 {
        0
    } else {
        ((*page_header).pd_lower - SizeOfPageHeaderData as u16)
            / std::mem::size_of::<pg_sys::ItemIdData>() as u16
    }
}

#[allow(non_snake_case)]
#[inline(always)]
#[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
pub unsafe fn BufferIsValid(buffer: pg_sys::Buffer) -> bool {
    // static inline bool
    // BufferIsValid(Buffer bufnum)
    // {
    //     Assert(bufnum <= NBuffers);
    //     Assert(bufnum >= -NLocBuffer);

    //     return bufnum != InvalidBuffer;
    // }
    assert!(buffer <= pg_sys::NBuffers);
    assert!(buffer >= -pg_sys::NLocBuffer);
    buffer != pg_sys::InvalidBuffer as pg_sys::Buffer
}
