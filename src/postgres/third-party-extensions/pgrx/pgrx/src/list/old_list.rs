use crate::{is_a, pg_sys, void_mut_ptr};
use std::marker::PhantomData;

pub struct PgList<T> {
    list: *mut pg_sys::List,
    allocated_by_pg: bool,
    _marker: PhantomData<T>,
}
impl<T> Default for PgList<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> PgList<T> {
    pub fn new() -> Self {
        PgList {
            list: std::ptr::null_mut(), // an empty List is NIL
            allocated_by_pg: false,
            _marker: PhantomData,
        }
    }

    pub unsafe fn from_pg(list: *mut pg_sys::List) -> Self {
        PgList { list, allocated_by_pg: true, _marker: PhantomData }
    }

    pub fn as_ptr(&self) -> *mut pg_sys::List {
        self.list
    }

    pub fn into_pg(mut self) -> *mut pg_sys::List {
        self.allocated_by_pg = true;
        self.list
    }

    #[inline]
    pub fn len(&self) -> usize {
        if self.list.is_null() {
            0
        } else {
            unsafe { self.list.as_ref() }.unwrap().length as usize
        }
    }

    #[inline]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    #[inline]
    pub fn head(&self) -> Option<*mut T> {
        if self.list.is_null() {
            None
        } else {
            Some(unsafe { pg_sys::list_nth(self.list, 0) } as *mut T)
        }
    }

    #[inline]
    pub fn tail(&self) -> Option<*mut T> {
        if self.list.is_null() {
            None
        } else {
            Some(unsafe { pg_sys::list_nth(self.list, (self.len() - 1) as i32) } as *mut T)
        }
    }

    #[inline]
    pub fn get_ptr(&self, i: usize) -> Option<*mut T> {
        if !self.is_empty()
            && unsafe { !is_a(self.list as *mut pg_sys::Node, pg_sys::NodeTag::T_List) }
        {
            panic!("PgList does not contain pointers")
        }
        if self.list.is_null() || i >= self.len() {
            None
        } else {
            Some(unsafe { pg_sys::list_nth(self.list, i as i32) } as *mut T)
        }
    }

    #[inline]
    pub fn get_int(&self, i: usize) -> Option<i32> {
        if !self.is_empty()
            && unsafe { !is_a(self.list as *mut pg_sys::Node, pg_sys::NodeTag::T_IntList) }
        {
            panic!("PgList does not contain ints")
        }

        if self.list.is_null() || i >= self.len() {
            None
        } else {
            Some(unsafe { pg_sys::list_nth_int(self.list, i as i32) })
        }
    }

    #[inline]
    pub fn get_oid(&self, i: usize) -> Option<pg_sys::Oid> {
        if !self.is_empty()
            && unsafe { !is_a(self.list as *mut pg_sys::Node, pg_sys::NodeTag::T_OidList) }
        {
            panic!("PgList does not contain oids")
        }

        if self.list.is_null() || i >= self.len() {
            None
        } else {
            Some(unsafe { pg_sys::list_nth_oid(self.list, i as i32) })
        }
    }

    #[inline]
    pub unsafe fn replace_ptr(&mut self, i: usize, with: *mut T) {
        let cell = pg_sys::list_nth_cell(self.list, i as i32);
        cell.as_mut().expect("cell is null").ptr_value = with as void_mut_ptr;
    }

    #[inline]
    pub fn replace_int(&mut self, i: usize, with: i32) {
        unsafe {
            let cell = pg_sys::list_nth_cell(self.list, i as i32);
            cell.as_mut().expect("cell is null").int_value = with;
        }
    }

    #[inline]
    pub fn replace_oid(&mut self, i: usize, with: pg_sys::Oid) {
        unsafe {
            let cell = pg_sys::list_nth_cell(self.list, i as i32);
            cell.as_mut().expect("cell is null").oid_value = with;
        }
    }

    #[inline]
    pub fn iter_ptr(&self) -> impl Iterator<Item = *mut T> + '_ {
        PgListIteratorPtr { list: self, pos: 0 }
    }

    #[inline]
    pub fn iter_oid(&self) -> impl Iterator<Item = pg_sys::Oid> + '_ {
        PgListIteratorOid { list: self, pos: 0 }
    }

    #[inline]
    pub fn iter_int(&self) -> impl Iterator<Item = i32> + '_ {
        PgListIteratorInt { list: self, pos: 0 }
    }

    /// Add a pointer value to the end of this list
    ///
    /// ## Safety
    ///
    /// We cannot guarantee the specified pointer is valid, but we assume it is as we only store it,
    /// we don't dereference it
    #[inline]
    pub fn push(&mut self, ptr: *mut T) {
        self.list = unsafe { pg_sys::lappend(self.list, ptr as void_mut_ptr) };
    }

    #[inline]
    pub fn pop(&mut self) -> Option<*mut T> {
        let tail = self.tail();

        if tail.is_some() {
            self.list = unsafe { pg_sys::list_truncate(self.list, (self.len() - 1) as i32) };
        }

        tail
    }
}

struct PgListIteratorPtr<'a, T> {
    list: &'a PgList<T>,
    pos: usize,
}

struct PgListIteratorOid<'a, T> {
    list: &'a PgList<T>,
    pos: usize,
}

struct PgListIteratorInt<'a, T> {
    list: &'a PgList<T>,
    pos: usize,
}

impl<'a, T> Iterator for PgListIteratorPtr<'a, T> {
    type Item = *mut T;

    fn next(&mut self) -> Option<Self::Item> {
        let result = self.list.get_ptr(self.pos);
        self.pos += 1;
        result
    }
}

impl<'a, T> Iterator for PgListIteratorOid<'a, T> {
    type Item = pg_sys::Oid;

    fn next(&mut self) -> Option<Self::Item> {
        let result = self.list.get_oid(self.pos);
        self.pos += 1;
        result
    }
}

impl<'a, T> Iterator for PgListIteratorInt<'a, T> {
    type Item = i32;

    fn next(&mut self) -> Option<Self::Item> {
        let result = self.list.get_int(self.pos);
        self.pos += 1;
        result
    }
}

impl<T> Drop for PgList<T> {
    fn drop(&mut self) {
        if !self.allocated_by_pg && !self.list.is_null() {
            unsafe {
                pg_sys::list_free(self.list);
            }
        }
    }
}
