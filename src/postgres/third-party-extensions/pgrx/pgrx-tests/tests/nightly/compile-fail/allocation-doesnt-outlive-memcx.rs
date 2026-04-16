#![feature(allocator_api)]

fn main() {}

fn allocation_outlive_memcx() {
    use pgrx::memcx::MemCx;
    use std::sync::Arc;

    let mut vector_used = Arc::new(None);

    pgrx::memcx::current_context(|mcx: &MemCx| {
        let mut inner_vec = Vec::new_in(&mcx);
        inner_vec.push(451);
        inner_vec.push(242);
        *Arc::<std::option::Option<_>>::get_mut(&mut vector_used).unwrap() = Some(inner_vec);
    });
    let _vector = Arc::<_>::get_mut(&mut vector_used).take().unwrap();
}
