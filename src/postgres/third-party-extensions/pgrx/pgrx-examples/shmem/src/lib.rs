//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx::atomics::*;
use pgrx::lwlock::PgLwLock;
use pgrx::prelude::*;
use pgrx::shmem::*;
use pgrx::{pg_shmem_init, warning};
use serde::*;
use std::sync::atomic::Ordering;

::pgrx::pg_module_magic!();

// types behind a `LwLock` must derive/implement `Copy` and `Clone`
#[derive(Copy, Clone)]
// This is for general Postgres type support -- not strictly necessary if the type is not exposed via SQL
#[derive(PostgresType, Serialize, Deserialize)]
#[derive(Default)]
pub struct Pgtest {
    value1: i32,
    value2: i32,
}

unsafe impl PGRXSharedMemory for Pgtest {}

static DEQUE: PgLwLock<heapless::Deque<Pgtest, 400>> = PgLwLock::new(c"shmem_deque");
static VEC: PgLwLock<heapless::Vec<Pgtest, 400>> = PgLwLock::new(c"shmem_vec");
static HASH: PgLwLock<heapless::FnvIndexMap<i32, i32, 4>> = PgLwLock::new(c"shmem_hash");
static STRUCT: PgLwLock<Pgtest> = PgLwLock::new(c"shmem_struct");
static PRIMITIVE: PgLwLock<i32> = PgLwLock::new(c"shmem_primtive");
static ATOMIC: PgAtomic<std::sync::atomic::AtomicBool> = PgAtomic::new(c"shmem_atomic");

#[pg_guard]
pub extern "C-unwind" fn _PG_init() {
    pg_shmem_init!(DEQUE);
    pg_shmem_init!(VEC);
    pg_shmem_init!(HASH);
    pg_shmem_init!(STRUCT);
    pg_shmem_init!(PRIMITIVE);
    pg_shmem_init!(ATOMIC);
}

#[pg_extern]
fn vec_select() -> SetOfIterator<'static, Pgtest> {
    SetOfIterator::new(VEC.share().iter().copied().collect::<Vec<Pgtest>>())
}

#[pg_extern]
fn vec_count() -> i32 {
    VEC.share().len() as i32
}

#[pg_extern]
fn vec_drain() -> SetOfIterator<'static, Pgtest> {
    let mut vec = VEC.exclusive();
    let r = vec.iter().copied().collect::<Vec<Pgtest>>();
    vec.clear();
    SetOfIterator::new(r)
}

#[pg_extern]
fn vec_push(value: Pgtest) {
    VEC.exclusive().push(value).unwrap_or_else(|_| warning!("Vector is full, discarding update"));
}

#[pg_extern]
fn vec_pop() -> Option<Pgtest> {
    VEC.exclusive().pop()
}

#[pg_extern]
fn deque_select() -> SetOfIterator<'static, Pgtest> {
    SetOfIterator::new(DEQUE.share().iter().copied().collect::<Vec<Pgtest>>())
}

#[pg_extern]
fn deque_count() -> i32 {
    DEQUE.share().len() as i32
}

#[pg_extern]
fn deque_drain() -> SetOfIterator<'static, Pgtest> {
    let mut vec = DEQUE.exclusive();
    let r = vec.iter().copied().collect::<Vec<Pgtest>>();
    vec.clear();
    SetOfIterator::new(r)
}

#[pg_extern]
fn deque_push_back(value: Pgtest) {
    DEQUE
        .exclusive()
        .push_back(value)
        .unwrap_or_else(|_| warning!("Deque is full, discarding update"));
}

#[pg_extern]
fn deque_push_front(value: Pgtest) {
    DEQUE
        .exclusive()
        .push_front(value)
        .unwrap_or_else(|_| warning!("Deque is full, discarding update"));
}

#[pg_extern]
fn deque_pop_back() -> Option<Pgtest> {
    DEQUE.exclusive().pop_back()
}

#[pg_extern]
fn deque_pop_front() -> Option<Pgtest> {
    DEQUE.exclusive().pop_front()
}

#[pg_extern]
fn hash_insert(key: i32, value: i32) {
    HASH.exclusive().insert(key, value).unwrap();
}

#[pg_extern]
fn hash_get(key: i32) -> Option<i32> {
    HASH.share().get(&key).cloned()
}

#[pg_extern]
fn struct_get() -> Pgtest {
    *STRUCT.share()
}

#[pg_extern]
fn struct_set(value1: i32, value2: i32) {
    *STRUCT.exclusive() = Pgtest { value1, value2 };
}

#[pg_extern]
fn primitive_get() -> i32 {
    *PRIMITIVE.share()
}

#[pg_extern]
fn primitive_set(value: i32) {
    *PRIMITIVE.exclusive() = value;
}

#[pg_extern]
fn atomic_get() -> bool {
    ATOMIC.get().load(Ordering::Relaxed)
}

#[pg_extern]
fn atomic_set(value: bool) -> bool {
    ATOMIC.get().swap(value, Ordering::Relaxed)
}
