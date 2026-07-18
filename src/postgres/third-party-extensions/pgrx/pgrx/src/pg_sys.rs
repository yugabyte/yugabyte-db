//! dirty hacks

// Flatten out the contents into here.
use crate::memcx;
use crate::ptr::PointerExt;
pub use pgrx_pg_sys::*;

// Interposing here can allow extensions like ZomboDB to skip the cshim,
// i.e. we are deliberately shadowing the upcoming names.

/**
```c
 #define rt_fetch(rangetable_index, rangetable) \
     ((RangeTblEntry *) list_nth(rangetable, (rangetable_index)-1))
```
*/
#[inline]
pub unsafe fn rt_fetch(index: Index, range_table: *mut List) -> *mut RangeTblEntry {
    memcx::current_context(|cx| {
        crate::list::List::<*mut core::ffi::c_void>::downcast_ptr_in_memcx(range_table, cx)
            .expect("rt_fetch used on non-ptr List")
            .get((index - 1) as _)
            .expect("rt_fetch used out-of-bounds")
            .cast()
    })
}

/**
```c
 /*
  * In places where it's known that simple_rte_array[] must have been prepared
  * already, we just index into it to fetch RTEs.  In code that might be
  * executed before or after entering query_planner(), use this macro.
  */
#define planner_rt_fetch(rti, root) \
    ((root)->simple_rte_array ? (root)->simple_rte_array[rti] : \
    rt_fetch(rti, (root)->parse->rtable))
```
*/
#[inline]
pub unsafe fn planner_rt_fetch(index: Index, root: *mut PlannerInfo) -> *mut RangeTblEntry {
    unsafe {
        if (*root).simple_rte_array.is_non_null() {
            *(*root).simple_rte_array.add(index as _)
        } else {
            rt_fetch(index, (*(*root).parse).rtable).cast()
        }
    }
}
