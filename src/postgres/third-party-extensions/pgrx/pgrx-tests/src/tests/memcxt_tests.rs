//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
#[cfg(any(test, feature = "pg_test"))]
#[pgrx::pg_schema]
mod tests {
    #[allow(unused_imports)]
    use crate as pgrx_tests;

    use pgrx::pg_test;
    use pgrx::prelude::*;
    use pgrx::PgMemoryContexts;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;

    struct TestObject {
        did_drop: Arc<AtomicBool>,
    }

    impl Drop for TestObject {
        fn drop(&mut self) {
            self.did_drop.store(true, Ordering::SeqCst);
        }
    }

    #[pg_test]
    fn test_leak_and_drop() {
        let did_drop = Arc::new(AtomicBool::new(false));

        unsafe {
            PgMemoryContexts::Transient {
                parent: PgMemoryContexts::CurrentMemoryContext.value(),
                name: "test",
                min_context_size: 4096,
                initial_block_size: 4096,
                max_block_size: 4096,
            }
            .switch_to(|context| {
                let test_object = TestObject { did_drop: did_drop.clone() };
                context.leak_and_drop_on_delete(test_object);
            });
        }

        assert!(did_drop.load(Ordering::SeqCst))
    }

    #[pg_test]
    fn test_leak_and_drop_with_panic() {
        let result = std::panic::catch_unwind(|| unsafe {
            struct Thing;
            impl Drop for Thing {
                fn drop(&mut self) {
                    panic!("please don't crash")
                }
            }

            PgMemoryContexts::Transient {
                parent: PgMemoryContexts::CurrentMemoryContext.value(),
                name: "test",
                min_context_size: 4096,
                initial_block_size: 4096,
                max_block_size: 4096,
            }
            .switch_to(|context| {
                context.leak_and_drop_on_delete(Thing);
            });
        });

        assert!(result.is_err());
        let err = result.unwrap_err();
        let caught = err.downcast_ref::<pg_sys::panic::CaughtError>();

        match caught {
            // would indicate some kind of terribleness with pgrx panic handling
            None => panic!("caught a panic that isn't a `pg_sys::panic::CaughtError`"),

            // this is the type of error we expect.  It comes to us here as the PostgresError
            // variant because, while it was a Rust panic, it was from a function with #[pg_guard]
            // directly called from Postgres
            Some(&pg_sys::panic::CaughtError::PostgresError(ref report))
                if report.message() == "please don't crash" =>
            {
                // ok!
            }

            // got some other kind of CaughtError variant we shouldn't have
            _ => panic!("did not catch the correct error/panic type: {caught:?}"),
        }
    }

    #[pg_test]
    fn parent() {
        unsafe {
            // SAFETY:  We know these two PgMemoryContext variants are valid b/c pgrx sets them up for us
            assert!(PgMemoryContexts::TopMemoryContext.parent().is_none());
            assert!(PgMemoryContexts::CurrentMemoryContext.parent().is_some());
        }
    }

    #[pg_test]
    fn switch_to_should_switch_back_on_panic() {
        let mut ctx = PgMemoryContexts::new("test");
        let ctx_sys = ctx.value();
        PgTryBuilder::new(move || unsafe {
            ctx.switch_to(|_| {
                assert!(std::ptr::eq(pg_sys::CurrentMemoryContext, ctx_sys));
                panic!();
            });
        })
        .catch_others(|_| {})
        .execute();
        assert_ne!(unsafe { pg_sys::CurrentMemoryContext }, ctx_sys);
    }

    #[pg_test]
    fn test_current_owned_memory_context_drop() {
        let mut ctx = PgMemoryContexts::new("test");
        let mut another_ctx = PgMemoryContexts::new("another");
        unsafe {
            another_ctx.set_as_current();
            ctx.set_as_current();
        }
        assert_eq!(unsafe { pg_sys::CurrentMemoryContext }, ctx.value());
        drop(ctx);
        assert_eq!(unsafe { pg_sys::CurrentMemoryContext }, another_ctx.value());
    }

    #[pg_test]
    fn test_current_owned_memory_context_drop_when_set_current_twice() {
        let ctx_parent = PgMemoryContexts::CurrentMemoryContext.value();
        let mut ctx = PgMemoryContexts::new("test");
        unsafe {
            ctx.set_as_current();
            ctx.set_as_current();
        }
        drop(ctx);
        assert_eq!(unsafe { pg_sys::CurrentMemoryContext }, ctx_parent);
    }

    #[cfg(feature = "nightly")]
    #[pg_test]
    fn memcx_allocator_test_string_vecs() {
        use pgrx::memcx::{current_context, MemCx};
        current_context(|mcx: &MemCx| {
            let str1 = "The Quick Brown Fox ";
            let str2 = "Jumped Over ";
            let str3 = "The Lazy Dog";

            let mut list: Vec<String, &MemCx> = Vec::new_in(mcx);

            assert_eq!(list, vec![] as Vec<String>);
            list.push(String::from(str1));
            assert_eq!(list, vec![String::from(str1)]);
            list.push(String::from(str2));
            assert_eq!(list, vec![String::from(str1), String::from(str2)]);

            let mut list_2: Vec<String, &MemCx> = Vec::new_in(mcx);
            assert_eq!(list_2, vec![] as Vec<String>);
            list_2.push(String::from(str3));
            assert_eq!(list_2, vec![String::from(str3)]);

            list.append(&mut list_2);
            assert_eq!(list_2, vec![] as Vec<String>);
            drop(list_2);
            assert_eq!(list, vec![String::from(str1), String::from(str2), String::from(str3)]);

            let mut list_3 = list.clone();

            let str4 = "The Quick Brown Ferret ";

            *list_3.get_mut(0).unwrap() = String::from(str4);
            assert_eq!(list_3, vec![String::from(str4), String::from(str2), String::from(str3)]);
            drop(list);
            assert_eq!(list_3, vec![String::from(str4), String::from(str2), String::from(str3)]);
        })
    }

    #[cfg(feature = "nightly")]
    #[pg_test]
    fn memcx_allocator_test_random_bytes() {
        use pgrx::memcx::{current_context, MemCx};
        current_context(|mcx: &MemCx| {
            let mut byte_buffers: Vec<Box<Vec<u8, &MemCx>, &MemCx>, &MemCx> = Vec::new_in(mcx);
            for _ in 0..32 {
                let mut current_buffer = Vec::new_in(mcx);
                for _ in 0..256 {
                    let number = rand::random();
                    current_buffer.push(number);
                }
                byte_buffers.push(Box::new_in(current_buffer, mcx));
            }
            assert_eq!(byte_buffers.len(), 32);

            for buf in byte_buffers.iter_mut() {
                assert_eq!(buf.len(), 256);
                for byte in buf.iter_mut() {
                    *byte = rand::random();
                }
                let a: [u8; 20] = rand::random();
                buf.extend_from_slice(&a);
                assert_eq!(buf.len(), 276);
            }

            byte_buffers.truncate(16);
            assert_eq!(byte_buffers.len(), 16);
            for buf in byte_buffers.iter_mut() {
                assert_eq!(buf.len(), 276);
                buf.truncate(77);
                assert_eq!(buf.len(), 77);
            }
        })
    }

    #[cfg(feature = "nightly")]
    #[pg_test]
    fn highly_aligned_type() {
        use pgrx::memcx::{current_context, MemCx};
        const BUF_SIZE: usize = 3319;

        #[repr(align(4096))]
        struct Foo {
            pub cool_id: u8,
            pub words: String,
            pub buf: [u8; BUF_SIZE],
        }
        impl Foo {
            // mcx used only for vec-building for buf, here.
            pub fn new(id: u8, memcx: &MemCx) -> Self {
                let mut buf_vec: Vec<u8, &MemCx> = Vec::new_in(memcx);
                for i in 0..BUF_SIZE {
                    buf_vec.push(Self::buf_value_for(id, i));
                }
                Foo { cool_id: id, words: format!("Buffer_{id}"), buf: buf_vec.try_into().unwrap() }
            }
            pub fn buf_value_for(id: u8, buffer_index: usize) -> u8 {
                ((buffer_index as u64 + id as u64) % 256) as u8
            }
            pub fn validate_content(&self) {
                assert_eq!(self.words, format!("Buffer_{}", self.cool_id));
                for (i, byte) in self.buf.iter().enumerate() {
                    assert_eq!(*byte, Self::buf_value_for(self.cool_id, i));
                }
            }
        }
        current_context(|mcx: &MemCx| {
            //Initialize
            let mut structures: Vec<Foo, &MemCx> = Vec::new_in(mcx);
            for i in 0..32 {
                let foo = Foo::new(i, mcx);
                structures.push(foo);
            }
            for foo in structures.iter() {
                foo.validate_content();
                let addr = (foo as *const Foo) as usize;
                assert!((addr % 4096) == 0);
            }
        });
    }
}
