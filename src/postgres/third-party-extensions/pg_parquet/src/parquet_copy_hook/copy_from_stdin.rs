use std::{ffi::CStr, io::Write};

use pgrx::{
    ffi::c_char,
    pg_sys::{
        makeStringInfo, pq_beginmessage, pq_copymsgbytes, pq_endmessage, pq_getmsgstring,
        pq_sendbyte, pq_sendint16, QueryCancelHoldoffCount, StringInfo,
    },
};

use crate::arrow_parquet::uri_utils::{uri_as_string, ParsedUriInfo};

/*
 * CopyFromStdinState is a simplified version of CopyFromState
 * in PostgreSQL to use in ReceiveDataFromClient with names
 * preserved.
 */
struct CopyFromStdinState {
    /* buffer in which we store incoming bytes */
    fe_msgbuf: StringInfo,

    /* whether we reached the end-of-file */
    raw_reached_eof: bool,
}

const MAX_READ_SIZE: usize = 65536;

/*
 * CopyInputToFile copies data from the socket to the given file.
 * We request the client send a specific column count.
 */
pub(crate) unsafe fn copy_stdin_to_file(uri_info: &ParsedUriInfo, natts: i16, is_binary: bool) {
    let mut cstate = CopyFromStdinState {
        fe_msgbuf: makeStringInfo(),
        raw_reached_eof: false,
    };

    /* open the destination file for writing */
    let path = uri_as_string(&uri_info.uri);

    // create or overwrite the local file
    let mut file = std::fs::OpenOptions::new()
        .write(true)
        .truncate(true)
        .create(true)
        .open(&path)
        .unwrap_or_else(|e| panic!("{}", e));

    /* tell the client we are ready for data */
    send_copy_in_begin(natts, is_binary);

    /* allocate on the heap since it's quite big */
    let mut receive_buffer = vec![0u8; MAX_READ_SIZE];

    while !cstate.raw_reached_eof {
        /* copy some bytes from the client into fe_msgbuf */
        let bytes_read = receive_data_from_client(&mut cstate, &mut receive_buffer);

        if bytes_read == 0 {
            break;
        }

        if bytes_read > 0 {
            /* copy bytes from fe_msgbuf to the destination file */
            file.write_all(&receive_buffer[..bytes_read])
                .unwrap_or_else(|e| {
                    panic!("could not write to file: {}", e);
                });
        }
    }
}

/*
 * send_copy_in_begin sends the CopyInResponse message that the client
 * expects after a COPY .. FROM STDIN.
 *
 * This code is adapted from ReceiveCopyBegin in PostgreSQL.
 */
unsafe fn send_copy_in_begin(natts: i16, is_binary: bool) {
    let buf = makeStringInfo();

    pq_beginmessage(buf, 'G' as _);

    let copy_format = if is_binary { 1 } else { 0 };
    pq_sendbyte(buf, copy_format);

    pq_sendint16(buf, natts as _);
    for _ in 0..natts {
        /* use the same format for all columns */
        pq_sendint16(buf, copy_format as _);
    }

    pq_endmessage(buf);
    ((*PqCommMethods).flush)();
}

const PQ_LARGE_MESSAGE_LIMIT: i32 = 1024 * 1024 * 1024 - 3;
const PQ_SMALL_MESSAGE_LIMIT: i32 = 10000;

unsafe fn receive_data_from_client(
    cstate: &mut CopyFromStdinState,
    receive_buffer: &mut [u8],
) -> usize {
    let mut databuf = receive_buffer;

    let minread = 1;
    let mut maxread = MAX_READ_SIZE;

    let mut bytesread = 0;

    while maxread > 0 && bytesread < minread && !cstate.raw_reached_eof {
        let mut avail;
        let mut flushed = false;

        while flushed || (*cstate.fe_msgbuf).cursor >= (*cstate.fe_msgbuf).len {
            /* Try to receive another message */

            QueryCancelHoldoffCount += 1;

            pq_startmsgread();

            let mtype = pq_getbyte();
            if mtype == -1 {
                panic!("unexpected EOF on client connection with an open transaction");
            }

            /* Validate message type and set packet size limit */
            let maxmsglen = match mtype as u8 as char {
                'd' =>
                /* CopyData */
                {
                    PQ_LARGE_MESSAGE_LIMIT
                }
                'c' | 'f' | 'H' | 'S' =>
                /* CopyDone, CopyFail, Flush, Sync */
                {
                    PQ_SMALL_MESSAGE_LIMIT
                }
                _ => {
                    panic!(
                        "unexpected message type 0x{:02X} during COPY from stdin",
                        mtype
                    );
                }
            };

            /* Now collect the message body */
            if pq_getmessage(cstate.fe_msgbuf, maxmsglen) != 0 {
                panic!("unexpected EOF on client connection with an open transaction");
            }

            QueryCancelHoldoffCount -= 1;

            /* ... and process it */
            match mtype as u8 as char {
                'd' => {
                    /* CopyData */
                    break;
                }
                'c' => {
                    /* CopyDone */
                    cstate.raw_reached_eof = true;
                    return bytesread;
                }
                'f' => {
                    /* CopyFail */
                    let msg = pq_getmsgstring(cstate.fe_msgbuf);
                    let msg = CStr::from_ptr(msg).to_str().expect("invalid CStr");
                    panic!("COPY from stdin failed: {msg}");
                }
                'H' | 'S' => {
                    /* Flush, Sync */
                    flushed = true;
                    continue;
                }
                _ => {
                    panic!(
                        "unexpected message type 0x{:02X} during COPY from stdin",
                        mtype
                    );
                }
            }
        }

        avail = ((*cstate.fe_msgbuf).len - (*cstate.fe_msgbuf).cursor) as _;
        if avail > maxread {
            avail = maxread;
        }

        pq_copymsgbytes(cstate.fe_msgbuf, databuf.as_mut_ptr() as _, avail as _);
        databuf = &mut databuf[avail..];
        maxread -= avail;
        bytesread += avail;
    }

    bytesread
}

// todo: move to pgrx (include libpq.h)
#[repr(C)]
struct PQcommMethods {
    comm_reset: unsafe extern "C" fn(),
    flush: unsafe extern "C" fn() -> i32,
    flush_if_writable: unsafe extern "C" fn() -> i32,
    is_send_pending: unsafe extern "C" fn() -> bool,
    putmessage: unsafe extern "C" fn(msgtype: u32, s: *const c_char, len: usize) -> i32,
    putmessage_noblock: unsafe extern "C" fn(msgtype: u32, s: *const c_char, len: usize),
}

unsafe extern "C" {
    fn pq_startmsgread();
    fn pq_getmessage(s: StringInfo, maxlen: i32) -> i32;
    fn pq_getbyte() -> i32;

    static PqCommMethods: *mut PQcommMethods;
}
