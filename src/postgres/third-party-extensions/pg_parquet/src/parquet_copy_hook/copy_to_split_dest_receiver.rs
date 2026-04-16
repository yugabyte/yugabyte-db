use std::{
    ffi::{c_char, CStr},
    path::Path,
};

use pg_sys::{AsPgCStr, CommandDest, DestReceiver, TupleDesc, TupleTableSlot};
use pgrx::prelude::*;

use crate::arrow_parquet::{
    compression::{PgParquetCompression, INVALID_COMPRESSION_LEVEL},
    field_ids::FieldIds,
    parquet_writer::{DEFAULT_ROW_GROUP_SIZE, DEFAULT_ROW_GROUP_SIZE_BYTES},
};

use super::copy_to_dest_receiver::{
    create_copy_to_parquet_dest_receiver, CopyToParquetDestReceiver,
};

pub(crate) const INVALID_FILE_SIZE_BYTES: i64 = 0;

#[repr(C)]
struct CopyToParquetSplitDestReceiver {
    dest: DestReceiver,
    uri: *const c_char,
    is_to_stdout: bool,
    tupledesc: TupleDesc,
    operation: i32,
    options: CopyToParquetOptions,
    current_child_id: i64,
    current_child_receiver: *mut CopyToParquetDestReceiver,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub(crate) struct CopyToParquetOptions {
    pub(crate) file_size_bytes: i64,
    pub(crate) field_ids: *const c_char,
    pub(crate) row_group_size: i64,
    pub(crate) row_group_size_bytes: i64,
    pub(crate) compression: PgParquetCompression,
    pub(crate) compression_level: i32,
}

impl CopyToParquetSplitDestReceiver {
    fn create_new_child(&mut self) {
        // create a new child receiver
        let child_uri = self.create_uri_for_child();
        self.current_child_receiver =
            create_copy_to_parquet_dest_receiver(child_uri, self.is_to_stdout, self.options);
        self.current_child_id += 1;

        // start the child receiver
        let child_parquet_dest =
            unsafe { PgBox::<DestReceiver>::from_pg(self.current_child_receiver as _) };

        if let Some(child_startup_callback) = child_parquet_dest.rStartup {
            unsafe {
                child_startup_callback(child_parquet_dest.as_ptr(), self.operation, self.tupledesc);
            }
        }
    }

    fn should_create_new_child(&self) -> bool {
        self.current_child_receiver.is_null()
    }

    fn send_tuple_to_child(&self, slot: *mut TupleTableSlot) {
        debug_assert!(!self.current_child_receiver.is_null());

        let child_parquet_dest =
            unsafe { PgBox::<DestReceiver>::from_pg(self.current_child_receiver as _) };

        if let Some(child_receive_callback) = child_parquet_dest.receiveSlot {
            unsafe {
                child_receive_callback(slot, child_parquet_dest.as_ptr());
            }
        }
    }

    fn flush_child(&mut self) {
        if self.current_child_receiver.is_null() {
            return;
        }

        // shutdown the current child receiver, which flushes collected tuples to parquet file.
        let child_parquet_dest =
            unsafe { PgBox::<DestReceiver>::from_pg(self.current_child_receiver as _) };

        if let Some(child_shutdown_callback) = child_parquet_dest.rShutdown {
            unsafe {
                child_shutdown_callback(child_parquet_dest.as_ptr());
            }
        }

        self.current_child_receiver = std::ptr::null_mut();
    }

    fn should_flush_child(&self) -> bool {
        if self.options.file_size_bytes == INVALID_FILE_SIZE_BYTES {
            return false;
        }

        debug_assert!(!self.current_child_receiver.is_null());

        let child_parquet_dest = unsafe {
            PgBox::<CopyToParquetDestReceiver>::from_pg(self.current_child_receiver as _)
        };

        child_parquet_dest.collected_bytes() as i64 > self.options.file_size_bytes
    }

    fn create_uri_for_child(&self) -> *const c_char {
        // file_size_bytes not specified, use the original uri
        if self.options.file_size_bytes == INVALID_FILE_SIZE_BYTES {
            return self.uri;
        }

        let uri = unsafe { CStr::from_ptr(self.uri) }
            .to_str()
            .expect("invalid uri");

        let parent_folder = Path::new(uri);

        let file_id = self.current_child_id;

        let child_uri = parent_folder.join(format!("data_{file_id}.parquet"));

        child_uri.to_str().expect("invalid uri").as_pg_cstr()
    }

    fn cleanup(&mut self) {
        if self.current_child_receiver.is_null() {
            return;
        }

        let mut child_parquet_dest = unsafe {
            PgBox::<CopyToParquetDestReceiver>::from_pg(self.current_child_receiver as _)
        };

        child_parquet_dest.cleanup();
    }
}

#[pg_guard]
extern "C-unwind" fn copy_split_startup(
    dest: *mut DestReceiver,
    operation: i32,
    tupledesc: TupleDesc,
) {
    let split_parquet_dest = unsafe {
        (dest as *mut CopyToParquetSplitDestReceiver)
            .as_mut()
            .expect("invalid split parquet dest receiver ptr")
    };

    // update the parquet split dest receiver's missing fields
    split_parquet_dest.operation = operation;
    split_parquet_dest.tupledesc = tupledesc;
    split_parquet_dest.create_new_child();
}

#[pg_guard]
extern "C-unwind" fn copy_split_receive(
    slot: *mut TupleTableSlot,
    dest: *mut DestReceiver,
) -> bool {
    let split_parquet_dest = unsafe {
        (dest as *mut CopyToParquetSplitDestReceiver)
            .as_mut()
            .expect("invalid split parquet dest receiver ptr")
    };

    if split_parquet_dest.should_create_new_child() {
        split_parquet_dest.create_new_child();
    }

    split_parquet_dest.send_tuple_to_child(slot);

    if split_parquet_dest.should_flush_child() {
        split_parquet_dest.flush_child();
    }

    true
}

#[pg_guard]
extern "C-unwind" fn copy_split_shutdown(dest: *mut DestReceiver) {
    let split_parquet_dest = unsafe {
        (dest as *mut CopyToParquetSplitDestReceiver)
            .as_mut()
            .expect("invalid split parquet dest receiver ptr")
    };

    split_parquet_dest.flush_child();
}

#[pg_guard]
extern "C-unwind" fn copy_split_destroy(_dest: *mut DestReceiver) {}

// create_copy_to_parquet_split_dest_receiver creates a new CopyToParquetSplitDestReceiver that can be
// used as a destination receiver for COPY TO command. All arguments, except "uri", are optional
// and have default values if not provided.
#[pg_guard]
#[no_mangle]
#[allow(clippy::too_many_arguments)]
pub extern "C-unwind" fn create_copy_to_parquet_split_dest_receiver(
    uri: *const c_char,
    is_to_stdout: bool,
    file_size_bytes: *const i64,
    field_ids: *const c_char,
    row_group_size: *const i64,
    row_group_size_bytes: *const i64,
    compression: *const PgParquetCompression,
    compression_level: *const i32,
) -> *mut DestReceiver {
    let file_size_bytes = if file_size_bytes.is_null() {
        INVALID_FILE_SIZE_BYTES
    } else {
        unsafe { *file_size_bytes }
    };

    let field_ids = if field_ids.is_null() {
        FieldIds::default().to_string().as_pg_cstr()
    } else {
        field_ids
    };

    let row_group_size = if row_group_size.is_null() {
        DEFAULT_ROW_GROUP_SIZE
    } else {
        unsafe { *row_group_size }
    };

    let row_group_size_bytes = if row_group_size_bytes.is_null() {
        DEFAULT_ROW_GROUP_SIZE_BYTES
    } else {
        unsafe { *row_group_size_bytes }
    };

    let compression = if compression.is_null() {
        PgParquetCompression::default()
    } else {
        unsafe { *compression }
    };

    let compression_level = if compression_level.is_null() {
        compression
            .default_compression_level()
            .unwrap_or(INVALID_COMPRESSION_LEVEL)
    } else {
        unsafe { *compression_level }
    };

    let options = CopyToParquetOptions {
        file_size_bytes,
        field_ids,
        row_group_size,
        row_group_size_bytes,
        compression,
        compression_level,
    };

    let mut split_dest =
        unsafe { PgBox::<CopyToParquetSplitDestReceiver, AllocatedByPostgres>::alloc0() };

    split_dest.dest.receiveSlot = Some(copy_split_receive);
    split_dest.dest.rStartup = Some(copy_split_startup);
    split_dest.dest.rShutdown = Some(copy_split_shutdown);
    split_dest.dest.rDestroy = Some(copy_split_destroy);
    split_dest.dest.mydest = CommandDest::DestCopyOut;
    split_dest.uri = uri;
    split_dest.is_to_stdout = is_to_stdout;
    split_dest.tupledesc = std::ptr::null_mut();
    split_dest.operation = -1;
    split_dest.options = options;
    split_dest.current_child_id = 0;

    unsafe { std::mem::transmute(split_dest) }
}

pub(crate) fn free_copy_to_parquet_split_dest_receiver(dest: *mut DestReceiver) {
    if dest.is_null() {
        return;
    }

    let split_dest = unsafe {
        (dest as *mut CopyToParquetSplitDestReceiver)
            .as_mut()
            .expect("invalid parquet dest receiver ptr")
    };

    split_dest.cleanup();
}
