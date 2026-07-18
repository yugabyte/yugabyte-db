//! Memory Contexts in PostgreSQL, now with lifetimes.
// "Why isn't this pgrx::mem or pgrx::memcxt?"
// Postgres actually uses all of:
// - mcxt
// - memcxt
// - mctx
// Search engines will see "memc[tx]{2}" and assume you mean memcpy!
// And it's nice-ish to have shorter lifetime names and have 'mcx consistently mean the lifetime.
use crate::pg_sys;
use core::{marker::PhantomData, ptr::NonNull};

/// A borrowed memory context.
pub struct MemCx<'mcx> {
    ptr: NonNull<pg_sys::MemoryContextData>,
    _marker: PhantomData<&'mcx pg_sys::MemoryContextData>,
}

impl<'mcx> MemCx<'mcx> {
    /// Wrap the provided [`pg_sys::MemoryContext`]
    ///
    /// # Safety
    /// Assumes the provided [`pg_sys::MemoryContext`] is valid and properly initialized.
    /// This method does check to ensure the pointer is non-null, but that is the only sanity
    /// check that is performed.
    pub(crate) unsafe fn from_ptr(raw: pg_sys::MemoryContext) -> MemCx<'mcx> {
        let ptr = NonNull::new(raw).expect("memory context must be non-null");
        MemCx { ptr, _marker: PhantomData }
    }

    /// Allocate a raw byte buffer `size` bytes in length
    /// and returns a pointer to the new allocation.
    pub fn alloc_bytes(&self, size: usize) -> *mut u8 {
        unsafe { pg_sys::MemoryContextAlloc(self.ptr.as_ptr(), size).cast() }
    }

    /// Stores the current memory context, switches to *this* memory context,
    /// and executes the closure `f`.
    /// Once `f` completes, the previous current memory context is restored.
    ///
    /// # Safety
    /// If `f` panics, the current memory context will remain set to this MemCx,
    /// and the previous current memory context will not be restored, leaving the entire
    /// Postgres environment in an invalid state.
    /// Please do not use this method with closures that can panic (of course, this is
    /// less of a concern for unit tests).
    pub unsafe fn exec_in<T>(&self, f: impl FnOnce() -> T) -> T {
        let remembered = pg_sys::MemoryContextSwitchTo(self.ptr.as_ptr());
        let res = f();
        pg_sys::MemoryContextSwitchTo(remembered);
        res
    }
}

/// Acquire the current context and operate inside it.
pub fn current_context<'curr, F, T>(f: F) -> T
where
    F: for<'clos> FnOnce(&'clos MemCx<'curr>) -> T,
{
    let memcx = unsafe { MemCx::from_ptr(pg_sys::YbCurrentMemoryContext) };

    f(&memcx)
}

#[cfg(all(feature = "nightly", feature = "pg16", feature = "pg17"))]
mod nightly {
    use super::*;
    use std::slice;

    unsafe impl<'mcx> std::alloc::Allocator for &MemCx<'mcx> {
        fn allocate(
            &self,
            layout: std::alloc::Layout,
        ) -> Result<NonNull<[u8]>, std::alloc::AllocError> {
            unsafe {
                // Bitflags for MemoryContextAllocAligned:
                // #define MCXT_ALLOC_HUGE    0x01 /* allow huge allocation (> 1 GB) */
                // #define MCXT_ALLOC_NO_OOM  0x02 /* no failure if out-of-memory */
                // #define MCXT_ALLOC_ZERO    0x04 /* zero allocated memory */
                let ptr = pg_sys::MemoryContextAllocAligned(
                    self.ptr.as_ptr(),
                    layout.size(),
                    layout.align(),
                    0,
                );
                let slice: &mut [u8] = slice::from_raw_parts_mut(ptr.cast(), layout.size());
                Ok(NonNull::new_unchecked(slice))
            }
        }

        unsafe fn deallocate(&self, ptr: NonNull<u8>, _layout: std::alloc::Layout) {
            // TODO: Find faster free for use when MemoryContext is known.
            // This is the global function that looks up the relevant Memory Context by address range.
            pg_sys::pfree(ptr.as_ptr().cast())
        }

        fn allocate_zeroed(
            &self,
            layout: std::alloc::Layout,
        ) -> Result<NonNull<[u8]>, std::alloc::AllocError> {
            // Overriding default function here to use Postgres' zeroing implementation.
            // Postgres 16 and newer permit any arbitrary power-of-2 alignment
            unsafe {
                // Bitflags for MemoryContextAllocAligned:
                // #define MCXT_ALLOC_HUGE    0x01 /* allow huge allocation (> 1 GB) */
                // #define MCXT_ALLOC_NO_OOM  0x02 /* no failure if out-of-memory */
                // #define MCXT_ALLOC_ZERO    0x04 /* zero allocated memory */
                let ptr = pg_sys::MemoryContextAllocAligned(
                    self.ptr.as_ptr(),
                    layout.size(),
                    layout.align(),
                    4,
                );
                let slice: &mut [u8] = slice::from_raw_parts_mut(ptr.cast(), layout.size());
                Ok(NonNull::new_unchecked(slice))
            }
        }
    }
}
