use super::{Enlist, List, ListCell, ListHead};
use crate::memcx::MemCx;
use crate::pg_sys;
use crate::ptr::PointerExt;
use crate::seal::Sealed;
use core::cmp;
use core::ffi;
use core::marker::PhantomData;
use core::mem;
use core::ops::{Bound, Deref, DerefMut, RangeBounds};
use core::ptr::{self, NonNull};
use core::slice;

impl<T: Enlist> Deref for ListCell<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // SAFETY: A brief upgrade of readonly &ListCell<T> to writable *mut pg_sys::ListCell
        // may seem sus, but is fine: Enlist::apoptosis is defined as pure casting/arithmetic.
        // So the pointer begins and ends without write permission, and
        // we essentially just reborrow a ListCell as its inner field type
        unsafe { &*T::apoptosis(&self.cell as *const _ as *mut _) }
    }
}

impl<T: Enlist> DerefMut for ListCell<T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        // SAFETY: we essentially just reborrow a ListCell as its inner field type which
        // only relies on pgrx::list::{Enlist, List, ListCell} maintaining safety invariants
        unsafe { &mut *T::apoptosis(&mut self.cell) }
    }
}

impl Sealed for *mut ffi::c_void {}
unsafe impl Enlist for *mut ffi::c_void {
    const LIST_TAG: pg_sys::NodeTag = pg_sys::NodeTag::T_List;

    unsafe fn apoptosis(cell: *mut pg_sys::ListCell) -> *mut *mut ffi::c_void {
        unsafe { ptr::addr_of_mut!((*cell).ptr_value) }
    }

    fn endocytosis(cell: &mut pg_sys::ListCell, value: Self) {
        cell.ptr_value = value;
    }
}

impl Sealed for ffi::c_int {}
unsafe impl Enlist for ffi::c_int {
    const LIST_TAG: pg_sys::NodeTag = pg_sys::NodeTag::T_IntList;

    unsafe fn apoptosis(cell: *mut pg_sys::ListCell) -> *mut ffi::c_int {
        unsafe { ptr::addr_of_mut!((*cell).int_value) }
    }

    fn endocytosis(cell: &mut pg_sys::ListCell, value: Self) {
        cell.int_value = value;
    }
}

impl Sealed for pg_sys::Oid {}
unsafe impl Enlist for pg_sys::Oid {
    const LIST_TAG: pg_sys::NodeTag = pg_sys::NodeTag::T_OidList;

    unsafe fn apoptosis(cell: *mut pg_sys::ListCell) -> *mut pg_sys::Oid {
        unsafe { ptr::addr_of_mut!((*cell).oid_value) }
    }

    fn endocytosis(cell: &mut pg_sys::ListCell, value: Self) {
        cell.oid_value = value;
    }
}

#[cfg(any(feature = "pg16", feature = "pg17"))]
impl Sealed for pg_sys::TransactionId {}
#[cfg(any(feature = "pg16", feature = "pg17"))]
unsafe impl Enlist for pg_sys::TransactionId {
    const LIST_TAG: pg_sys::NodeTag = pg_sys::NodeTag::T_XidList;

    unsafe fn apoptosis(cell: *mut pg_sys::ListCell) -> *mut pg_sys::TransactionId {
        unsafe { ptr::addr_of_mut!((*cell).xid_value) }
    }

    fn endocytosis(cell: &mut pg_sys::ListCell, value: Self) {
        cell.xid_value = value;
    }
}

impl<'cx, T: Enlist> List<'cx, T> {
    /// Borrow an item from the List at the index
    pub fn get(&self, index: usize) -> Option<&T> {
        self.as_cells().get(index).map(Deref::deref)
    }

    /// Mutably borrow an item from the List at the index
    pub fn get_mut(&mut self, index: usize) -> Option<&mut T> {
        self.as_cells_mut().get_mut(index).map(DerefMut::deref_mut)
    }

    /// Pushes an item into the List
    ///
    /// Allocates the entire list in referenced context if it had zero elements,
    /// otherwise uses the List's own context.
    ///
    /// "Unstable" because this may receive breaking changes.
    pub fn unstable_push_in_context(
        &mut self,
        value: T,
        mcx: &'cx MemCx<'_>,
    ) -> &mut ListHead<'cx, T> {
        match self {
            List::Nil => {
                // No silly reasoning, simply allocate ~2 cache lines for a list
                let list_size = 128;
                unsafe {
                    let list: *mut pg_sys::List = mcx.alloc_bytes(list_size).cast();
                    assert!(list.is_non_null());
                    (*list).type_ = T::LIST_TAG;
                    (*list).max_length = ((list_size - mem::size_of::<pg_sys::List>())
                        / mem::size_of::<pg_sys::ListCell>())
                        as _;
                    (*list).elements = ptr::addr_of_mut!((*list).initial_elements).cast();
                    T::endocytosis((*list).elements.as_mut().unwrap(), value);
                    (*list).length = 1;
                    *self = Self::downcast_ptr_in_memcx(list, mcx).unwrap();
                    assert_eq!(1, self.len());
                    match self {
                        List::Cons(head) => head,
                        _ => unreachable!(),
                    }
                }
            }
            List::Cons(head) => head.push(value),
        }
    }

    // Iterate over part of the List while removing elements from it
    //
    // Note that if this removes the last item, it deallocates the entire list.
    // This is to maintain the Postgres List invariant that a 0-len list is always Nil.
    pub fn drain<R>(&mut self, range: R) -> Drain<'_, 'cx, T>
    where
        R: RangeBounds<usize>,
    {
        // SAFETY: The Drain invariants are somewhat easier to maintain for List than Vec,
        // however, they have the complication of the Postgres List invariants
        let len = self.len();
        let drain_start = match range.start_bound() {
            Bound::Unbounded | Bound::Included(0) => 0,
            Bound::Included(first) => *first,
            Bound::Excluded(point) => point + 1,
        };
        let tail_start = match range.end_bound() {
            Bound::Unbounded => cmp::min(ffi::c_int::MAX as _, len),
            Bound::Included(last) => last + 1,
            Bound::Excluded(tail) => *tail,
        };
        let Some(tail_len) = len.checked_sub(tail_start) else {
            panic!("index out of bounds of list!")
        };
        // Let's issue our asserts before mutating state:
        assert!(drain_start <= len);
        assert!(tail_start <= len);

        // Postgres assumes Lists fit into c_int, check before shrinking
        assert!(tail_start <= ffi::c_int::MAX as _);
        assert!(drain_start + tail_len <= ffi::c_int::MAX as _);

        // If draining all, rip it out of place to contain broken invariants from panics
        let raw = if drain_start == 0 {
            mem::take(self).into_ptr()
        } else {
            // Leave it in place, but we need a pointer:
            match self {
                List::Nil => ptr::null_mut(),
                List::Cons(head) => head.list.as_ptr().cast(),
            }
        };

        // Remember to check that our raw ptr is non-null
        if raw.is_non_null() {
            // Shorten the list to prohibit interaction with List's state after drain_start.
            // Note this breaks List repr invariants in the `drain_start == 0` case, but
            // we only consider returning the list ptr to `&mut self` if Drop is completed
            unsafe { (*raw).length = drain_start as _ };
            let cells_ptr = unsafe { (*raw).elements };
            let iter = unsafe {
                RawCellIter {
                    ptr: cells_ptr.add(drain_start).cast(),
                    end: cells_ptr.add(tail_start).cast(),
                }
            };
            Drain { tail_len: tail_len as _, tail_start: tail_start as _, raw, origin: self, iter }
        } else {
            // If it's not, produce the only valid choice: a 0-len iterator pointing to null
            // One last doublecheck for old paranoia's sake:
            assert!(tail_len == 0 && tail_start == 0 && drain_start == 0);
            Drain { tail_len: 0, tail_start: 0, raw, origin: self, iter: Default::default() }
        }
    }

    pub fn iter(&self) -> impl Iterator<Item = &T> {
        self.as_cells().into_iter().map(Deref::deref)
    }

    pub fn iter_mut(&mut self) -> impl Iterator<Item = &mut T> {
        self.as_cells_mut().into_iter().map(DerefMut::deref_mut)
    }
}

impl<T> List<'_, T> {
    /// Borrow the List's slice of cells
    ///
    /// Note that like with Vec, this slice may move after appending to the List!
    /// Due to lifetimes this isn't a problem until unsafe Rust becomes involved,
    /// but with Postgres extensions it often does.
    ///
    /// Note that if you use this on a 0-item list, you get an empty slice,
    /// which is not going to be equal to the null pointer.
    pub fn as_cells(&self) -> &[ListCell<T>] {
        unsafe {
            match self {
                List::Nil => &[],
                List::Cons(inner) => slice::from_raw_parts(inner.as_cells_ptr(), inner.len()),
            }
        }
    }

    /// Mutably borrow the List's slice of cells
    ///
    /// Includes the same caveats as with `List::as_cells`, but with "less" problems:
    /// `&mut` means you should not have other pointers to the list anyways.
    ///
    /// Note that if you use this on a 0-item list, you get an empty slice,
    /// which is not going to be equal to the null pointer.
    pub fn as_cells_mut(&mut self) -> &mut [ListCell<T>] {
        // SAFETY: Note it is unsafe to read a union variant, but safe to set a union variant!
        // This allows access to `&mut pg_sys::ListCell` to mangle a List's type in safe code.
        // Also note that we can't yield &mut [T] because Postgres Lists aren't tight-packed.
        // These facts are why the entire List type's interface isn't much simpler.
        //
        // This function is safe as long as ListCell<T> offers no way to corrupt the list,
        // and as long as we correctly maintain the length of the List's type.
        unsafe {
            match self {
                List::Nil => &mut [],
                List::Cons(inner) => {
                    slice::from_raw_parts_mut(inner.as_mut_cells_ptr(), inner.len())
                }
            }
        }
    }
}

impl<T> ListHead<'_, T> {
    #[inline]
    pub fn capacity(&self) -> usize {
        unsafe { self.list.as_ref().max_length as usize }
    }

    /// Borrow the List's slice of cells
    ///
    /// Note that like with Vec, this slice may move after appending to the List!
    /// Due to lifetimes this isn't a problem until unsafe Rust becomes involved,
    /// but with Postgres extensions it often does.
    pub fn as_cells(&self) -> &[ListCell<T>] {
        unsafe { slice::from_raw_parts(self.as_cells_ptr(), self.len()) }
    }

    pub fn as_cells_ptr(&self) -> *const ListCell<T> {
        unsafe { (*self.list.as_ptr()).elements.cast() }
    }

    pub fn as_mut_cells_ptr(&mut self) -> *mut ListCell<T> {
        unsafe { (*self.list.as_ptr()).elements.cast() }
    }
}

impl<T: Enlist> ListHead<'_, T> {
    pub fn push(&mut self, value: T) -> &mut Self {
        let list = unsafe { self.list.as_mut() };
        let pg_sys::List { length, max_length, elements, .. } = list;
        assert!(*max_length > 0);
        assert!(*length > 0);
        assert!(*max_length >= *length);
        if *max_length - *length < 1 {
            self.reserve(*max_length as _);
        }

        // SAFETY: Our list must have been constructed following the list invariants
        // in order to actually get here, and we have confirmed as in-range of the buffer.
        let cell = unsafe { &mut *elements.add(*length as _) };
        T::endocytosis(cell, value);
        *length += 1;
        self
    }

    pub fn reserve(&mut self, count: usize) -> &mut Self {
        let list = unsafe { self.list.as_mut() };
        assert!(list.length > 0);
        assert!(list.max_length > 0);
        if ((list.max_length - list.length) as usize) < count {
            let size = i32::try_from(count).unwrap();
            let size = list.length.checked_add(size).unwrap();
            let size = usize::try_from(size).unwrap();
            unsafe { grow_list(list, size) };
        };
        self
    }
}

unsafe fn grow_list(list: &mut pg_sys::List, target: usize) {
    assert!((i32::MAX as usize) >= target, "Cannot allocate more than c_int::MAX elements");
    let alloc_size = target * mem::size_of::<pg_sys::ListCell>();
    if list.elements == ptr::addr_of_mut!(list.initial_elements).cast() {
        // first realloc, we can't dealloc the elements ptr, as it isn't its own alloc
        let context = pg_sys::GetMemoryChunkContext(list as *mut pg_sys::List as *mut _);
        if context.is_null() {
            panic!("Context free list?");
        }
        let buf = pg_sys::MemoryContextAlloc(context, alloc_size);
        if buf.is_null() {
            panic!("List allocation failure");
        }
        ptr::copy_nonoverlapping(list.elements, buf.cast(), list.length as _);
        // This is the "clobber pattern" that Postgres uses.
        #[cfg(debug_assertions)]
        ptr::write_bytes(list.elements, 0x7F, list.length as _);
        list.elements = buf.cast();
    } else {
        // We already have a separate buf, making this easy.
        list.elements = pg_sys::repalloc(list.elements.cast(), alloc_size).cast();
    }

    list.max_length = target as _;
}

unsafe fn destroy_list(list: *mut pg_sys::List) {
    // The only question is if we have two allocations or one?
    if (*list).elements != ptr::addr_of_mut!((*list).initial_elements).cast() {
        pg_sys::pfree((*list).elements.cast());
    }
    pg_sys::pfree(list.cast());
}

#[derive(Debug)]
pub struct ListIter<'a, T> {
    list: List<'a, T>,
    iter: RawCellIter<T>,
}

/// A list being drained.
#[derive(Debug)]
pub struct Drain<'a, 'cx, T> {
    /// Index of tail to preserve
    tail_start: u32,
    /// Length of tail
    tail_len: u32,
    /// Current remaining range to remove
    iter: RawCellIter<T>,
    origin: &'a mut List<'cx, T>,
    raw: *mut pg_sys::List,
}

impl<'a, 'cx, T> Drop for Drain<'a, 'cx, T> {
    fn drop(&mut self) {
        if self.raw.is_null() {
            return;
        }

        // SAFETY: The raw repr accepts null ptrs, but we just checked it's okay.
        unsafe {
            // Note that this may be 0, unlike elsewhere!
            let len = (*self.raw).length;
            if len == 0 && self.tail_len == 0 {
                // Can't simply leave it be due to Postgres List invariants, else it leaks
                destroy_list(self.raw)
            } else {
                // Need to weld over the drained part and fix the length
                let src = (*self.raw).elements.add(self.tail_start as _);
                let dst = (*self.raw).elements.add(len as _);
                ptr::copy(src, dst, self.tail_len as _); // may overlap
                (*self.raw).length = len + (self.tail_len as ffi::c_int);

                // Put it back now that all invariants have been repaired
                *self.origin = List::Cons(ListHead {
                    list: NonNull::new_unchecked(self.raw),
                    _type: PhantomData,
                });
            }
        }
    }
}

impl<T: Enlist> Iterator for Drain<'_, '_, T> {
    type Item = T;

    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next()
    }
}

impl<T: Enlist> Iterator for ListIter<'_, T> {
    type Item = T;

    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next()
    }
}

impl<'a, T: Enlist> IntoIterator for List<'a, T> {
    type IntoIter = ListIter<'a, T>;
    type Item = T;

    fn into_iter(mut self) -> Self::IntoIter {
        let len = self.len();
        let iter = match &mut self {
            List::Nil => Default::default(),
            List::Cons(head) => {
                let ptr = head.as_mut_cells_ptr();
                let end = unsafe { ptr.add(len) };
                RawCellIter { ptr, end }
            }
        };
        ListIter { list: self, iter }
    }
}

impl<'a, T> Drop for ListIter<'a, T> {
    fn drop(&mut self) {
        if let List::Cons(head) = &mut self.list {
            unsafe { destroy_list(head.list.as_ptr()) }
        }
    }
}

/// Needed because otherwise List hits incredibly irritating lifetime issues.
///
/// This must remain a private type, as casual usage of it is wildly unsound.
///
/// # Safety
/// None. Repent that you made this.
///
/// This atrocity assumes pointers passed in are valid or that ptr >= end.
#[derive(Debug, PartialEq)]
struct RawCellIter<T> {
    ptr: *mut ListCell<T>,
    end: *mut ListCell<T>,
}

impl<T> Default for RawCellIter<T> {
    fn default() -> Self {
        RawCellIter { ptr: ptr::null_mut(), end: ptr::null_mut() }
    }
}

impl<T: Enlist> Iterator for RawCellIter<T> {
    type Item = T;

    #[inline]
    fn next(&mut self) -> Option<T> {
        if self.ptr < self.end {
            let ptr = self.ptr;
            // SAFETY: It's assumed that the pointers are valid on construction
            unsafe {
                self.ptr = ptr.add(1);
                Some(T::apoptosis(ptr.cast()).read())
            }
        } else {
            None
        }
    }
}
