#![deny(unsafe_op_in_unsafe_fn)]
use pgrx::prelude::*;
use serde::ser::{SerializeStruct, Serializer};
use serde::Serialize;
use std::alloc::{alloc, dealloc, Layout};

::pgrx::pg_module_magic!();

// An Action describe a change that occurred on a table
#[derive(Serialize)]
struct Action {
    typ: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    committed: Option<i64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    rel: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    old: Option<Tuple>,
    #[serde(skip_serializing_if = "Option::is_none")]
    new: Option<Tuple>,
    #[serde(skip_serializing_if = "Option::is_none")]
    change_count: Option<i64>,
}

// Multiple constructors depending on the type of action logged
impl Action {
    // This is a simple BEGIN Statement
    pub fn begin() -> Self {
        Self {
            typ: "BEGIN".into(),
            committed: None,
            rel: None,
            old: None,
            new: None,
            change_count: None,
        }
    }

    // This is a simple COMMIT Statement
    pub fn commit(txn: PgBox<pg_sys::ReorderBufferTXN>, change_count: i64) -> Self {
        Self {
            typ: "COMMIT".into(),
            // TODO: convert the commit timestamp into a human readable format ?
            committed: Some(txn.commit_time),
            rel: None,
            old: None,
            new: None,
            change_count: Some(change_count),
        }
    }

    // A change can be either an INSERT, a DELETE or an UPDATE
    pub fn change(rel: pgrx::PgRelation, change: PgBox<pg_sys::ReorderBufferChange>) -> Self {
        use pgrx::pg_sys::ReorderBufferChangeType::*;
        use pgrx::spi;

        Self {
            typ: match change.action {
                REORDER_BUFFER_CHANGE_DELETE => "DELETE".into(),
                REORDER_BUFFER_CHANGE_INSERT => "INSERT".into(),
                REORDER_BUFFER_CHANGE_UPDATE => "UPDATE".into(),
                _ => "Unknown".into(),
            },
            committed: None,
            rel: Some(format!(
                "{}.{}",
                spi::quote_identifier(rel.namespace()),
                spi::quote_identifier(rel.name())
            )),

            // old tuple (updated or deleted)
            //
            // For UPDATE, the oldtuple is only provided when :
            //  - REPLICA IDENTITY is FULL
            //  - The primary key is changed
            //  - replica identity is index and indexed column changes.
            //
            // Outside of these 3 cases, the `old` PgBox will contain a null
            // pointer !
            //
            // TODO: when the oldtuple is not available, we should fetch the
            // index values with RelationGetIndexAttrBitmap
            //
            old: match change.action {
                REORDER_BUFFER_CHANGE_UPDATE | REORDER_BUFFER_CHANGE_DELETE => Some(Tuple {
                    rel: rel.clone(),
                    data: unsafe { PgBox::from_pg(change.data.tp.oldtuple) },
                }),
                _ => None,
            },

            // the new tuple (updated or inserted)
            new: match change.action {
                REORDER_BUFFER_CHANGE_UPDATE | REORDER_BUFFER_CHANGE_INSERT => Some(Tuple {
                    rel: rel.clone(),
                    data: unsafe { PgBox::from_pg(change.data.tp.newtuple) },
                }),
                _ => None,
            },
            change_count: None,
        }
    }
}

// Serialize an Action into a JSON string and write it to the plugin output
trait OutputPluginWrite {
    fn output_plugin_write(&self, ctx: PgBox<pg_sys::LogicalDecodingContext>)
    where
        Self: Serialize,
    {
        use std::ffi::CString;

        unsafe {
            pg_sys::OutputPluginPrepareWrite(ctx.as_ptr(), true);
        }
        // Serialize yourself to a JSON string.
        let json = serde_json::to_string(&self).expect("Serde Error");
        let json_cstring = CString::new(json).unwrap();
        unsafe {
            pg_sys::appendStringInfo(ctx.out, json_cstring.as_c_str().as_ptr());
            pg_sys::OutputPluginWrite(ctx.as_ptr(), true);
        }
    }
}

impl OutputPluginWrite for Action {}

// The decoding state will be an object shared between the callbacks
// It will track how many changes occurred during a transaction
struct DecodingState {
    xact_change_counter: i64,
}

// A Tuple describes the values of a table row before or after a change
struct Tuple {
    rel: pgrx::PgRelation,
    data: PgBox<pg_sys::ReorderBufferTupleBuf>,
}

// Loop over the Tuple attributes and serialize them
impl Serialize for Tuple {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        use std::ffi::CStr;

        let desc = self.rel.tuple_desc();
        let mut isnull: bool = false;

        let mut serde_state = serializer.serialize_struct("Tuple", desc.len())?;

        for attribute in desc.iter() {
            if self.data.is_null() {
                continue;
            }
            let attname = unsafe {
                CStr::from_ptr(pg_sys::quote_identifier(attribute.attname.data.as_ptr()))
            }
            .to_str()
            .unwrap();
            let mut tuple = self.data.tuple;
            let datum = unsafe {
                pg_sys::heap_getattr(
                    &mut tuple,
                    attribute.attnum.into(),
                    desc.as_ptr(),
                    &mut isnull,
                )
            };
            if isnull {
                continue;
            }
            match attribute.atttypid {
                pg_sys::INT4OID => {
                    let value = unsafe { i32::from_datum(datum, isnull) };
                    serde_state.serialize_field(attname, &(value.unwrap()))?
                }
                pg_sys::TEXTOID => {
                    let value = unsafe { String::from_datum(datum, isnull) };
                    serde_state.serialize_field(attname, &(value.unwrap()))?
                }
                _ => todo!(),
            };
        }
        serde_state.end()
    }
}

// Initialize the output plugin
#[allow(non_snake_case)]
#[no_mangle]
#[pg_guard]
pub unsafe extern "C-unwind" fn _PG_output_plugin_init(cb_ptr: *mut pg_sys::OutputPluginCallbacks) {
    let mut callbacks = unsafe { PgBox::from_pg(cb_ptr) };
    callbacks.startup_cb = Some(pg_decode_startup);
    callbacks.begin_cb = Some(pg_decode_begin_txn);
    callbacks.change_cb = Some(pg_decode_change);
    callbacks.commit_cb = Some(pg_decode_commit_txn);
    callbacks.shutdown_cb = Some(pg_decode_shutdown);
    callbacks.into_pg();
    debug1!("Anon: output plugin initialized");
}

// Callbacks
//
// The complete list of callbacks is available at:
// https://www.postgresql.org/docs/current/logicaldecoding-output-plugin.html
//

#[pg_guard]
unsafe extern "C-unwind" fn pg_decode_startup(
    ctx_ptr: *mut pg_sys::LogicalDecodingContext,
    options_ptr: *mut pg_sys::OutputPluginOptions,
    _is_init: bool,
) {
    let mut options = unsafe { PgBox::from_pg(options_ptr) };
    options.output_type = pg_sys::OutputPluginOutputType::OUTPUT_PLUGIN_TEXTUAL_OUTPUT;
    options.into_pg();

    // output_plugin_private is used to store a pointer to a common state shared
    // between all the callbacks of the same transaction. There may be a better
    // way to manage this pointer, but currently alloc/dealloc the cleanest way
    // I could think of...
    let layout = Layout::new::<DecodingState>();
    let mut ctx = unsafe { PgBox::from_pg(ctx_ptr) };
    ctx.output_plugin_private = unsafe { alloc(layout) as *mut std::ffi::c_void };
    ctx.into_pg();
}

#[pg_guard]
unsafe extern "C-unwind" fn pg_decode_begin_txn(
    ctx_ptr: *mut pg_sys::LogicalDecodingContext,
    _txn_ptr: *mut pg_sys::ReorderBufferTXN,
) {
    let ctx = unsafe { PgBox::from_pg(ctx_ptr) };
    let mut state = unsafe { PgBox::from_pg(ctx.output_plugin_private as *mut DecodingState) };
    state.xact_change_counter = 0;
    Action::begin().output_plugin_write(ctx);
}

#[pg_guard]
unsafe extern "C-unwind" fn pg_decode_commit_txn(
    ctx_ptr: *mut pg_sys::LogicalDecodingContext,
    txn_ptr: *mut pg_sys::ReorderBufferTXN,
    _commit_lsn: pg_sys::XLogRecPtr,
) {
    let ctx = unsafe { PgBox::from_pg(ctx_ptr) };
    let txn = unsafe { PgBox::from_pg(txn_ptr) };
    let state = unsafe { PgBox::from_pg(ctx.output_plugin_private as *mut DecodingState) };
    Action::commit(txn, state.xact_change_counter).output_plugin_write(ctx);
}

#[pg_guard]
unsafe extern "C-unwind" fn pg_decode_change(
    ctx_ptr: *mut pg_sys::LogicalDecodingContext,
    _txn_ptr: *mut pg_sys::ReorderBufferTXN,
    relation: pg_sys::Relation,
    change_ptr: *mut pg_sys::ReorderBufferChange,
) {
    let ctx = unsafe { PgBox::from_pg(ctx_ptr) };
    let mut state = unsafe { PgBox::from_pg(ctx.output_plugin_private as *mut DecodingState) };
    state.xact_change_counter += 1;

    let pgrelation = unsafe { pgrx::PgRelation::from_pg(relation) };
    let change = unsafe { PgBox::from_pg(change_ptr) };

    Action::change(pgrelation, change).output_plugin_write(ctx);
}

#[pg_guard]
unsafe extern "C-unwind" fn pg_decode_shutdown(ctx_ptr: *mut pg_sys::LogicalDecodingContext) {
    let layout = Layout::new::<DecodingState>();
    let ctx = unsafe { PgBox::from_pg(ctx_ptr) };
    unsafe { dealloc(ctx.output_plugin_private as *mut u8, layout) };
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use pgrx::prelude::*;

    #[pg_test]
    fn test_action_begin() {
        use crate::Action;

        let input = Action::begin();
        let json = serde_json::to_string(&input).expect("Serde Error");
        assert_eq!(json, r#"{"typ":"BEGIN"}"#);
    }

    #[pg_test]
    #[ignore]
    fn test_callbacks() {
        //
        // Not sure how to handle the tests of the callbacks....
        // This function is running *inside* a transaction
        // and the WAL decoder will output the changes only when they are
        // committed.
        //
    }
}

/// This module is required by `cargo pgrx test` invocations.
/// It must be visible at the root of your extension crate.
#[cfg(test)]
pub mod pg_test {
    pub fn setup(_options: Vec<&str>) {
        // perform one-off initialization when the pg_test framework starts
    }

    #[must_use]
    pub fn postgresql_conf_options() -> Vec<&'static str> {
        // return any postgresql.conf settings that are required for your tests
        vec![]
    }
}

// <!-- EOL -->
