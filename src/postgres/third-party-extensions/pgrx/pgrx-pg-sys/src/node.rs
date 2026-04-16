use core::ffi::CStr;
use core::ptr::NonNull;

/// A trait applied to all Postgres `pg_sys::Node` types and subtypes
pub trait PgNode: crate::seal::Sealed {
    /// Format this node
    ///
    /// # Safety
    ///
    /// While pgrx controls the types for which [`PgNode`] is implemented and only pgrx can implement
    /// [`PgNode`] it cannot control the members of those types and a user could assign any pointer
    /// type member to something invalid that Postgres wouldn't understand.
    ///
    /// Because this function is used by `impl Display` we purposely don't mark it `unsafe`.  The
    /// assumption here, which might be a bad one, is that only intentional misuse would actually
    /// cause undefined behavior.
    #[inline]
    fn display_node(&self) -> String {
        // SAFETY: The trait is pub but this impl is private, and
        // this is only implemented for things known to be "Nodes"
        unsafe { display_node_impl(NonNull::from(self).cast()) }
    }
}

/// implementation function for `impl Display for $NodeType`
///
/// # Safety
/// Don't use this on anything that doesn't impl PgNode, or the type may be off
#[warn(unsafe_op_in_unsafe_fn)]
pub(crate) unsafe fn display_node_impl(node: NonNull<crate::Node>) -> String {
    // SAFETY: It's fine to call nodeToString with non-null well-typed pointers,
    // and pg_sys::nodeToString() returns data via palloc, which is never null
    // as Postgres will ERROR rather than giving us a null pointer,
    // and Postgres starts and finishes constructing StringInfos by writing '\0'
    unsafe {
        let node_cstr = crate::nodeToString(node.as_ptr().cast());

        let result = match CStr::from_ptr(node_cstr).to_str() {
            Ok(cstr) => cstr.to_string(),
            Err(e) => format!("<ffi error: {e:?}>"),
        };

        crate::pfree(node_cstr.cast());

        result
    }
}
