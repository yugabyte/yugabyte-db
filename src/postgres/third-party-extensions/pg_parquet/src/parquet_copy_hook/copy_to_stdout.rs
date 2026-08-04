use std::{fs::File, io::Read};

use pgrx::pg_sys::{
    makeStringInfo, pq_beginmessage, pq_endmessage, pq_putemptymessage, pq_sendbyte, pq_sendbytes,
    pq_sendint16,
};

use crate::arrow_parquet::uri_utils::{uri_as_string, ParsedUriInfo};

/*
 * copy_file_to_stdout copies the raw contents of a file to the
 * client as part of a COPY .. TO STDOUT.
 */
pub(crate) unsafe fn copy_file_to_stdout(uri_info: ParsedUriInfo, natts: i16) {
    let path = uri_as_string(&uri_info.uri);

    let mut file = File::open(path).unwrap_or_else(|e| {
        panic!("could not open temp file: {}", e);
    });

    let is_binary = true;
    send_copy_begin(natts, is_binary);

    /* allocate on the heap since it's quite big */
    const MAX_READ_SIZE: usize = 65536;
    let mut send_buffer = vec![0u8; MAX_READ_SIZE];

    loop {
        let bytes_read = file.read(&mut send_buffer).unwrap_or_else(|e| {
            panic!("could not read from temp file: {}", e);
        });

        if bytes_read == 0 {
            break;
        }

        send_copy_data(&send_buffer[..bytes_read]);
    }

    send_copy_end();
}

/*
 * send_copy_begin sends the CopyOutResponse message to start a
 * COPY .. TO STDOUT.
 *
 * This code is adapted from SendCopyBegin in PostgreSQL.
 */
unsafe fn send_copy_begin(natts: i16, is_binary: bool) {
    let buf = makeStringInfo();

    pq_beginmessage(buf, 'H' as _);

    let copy_format = if is_binary { 1 } else { 0 };
    pq_sendbyte(buf, copy_format); /* overall format */

    pq_sendint16(buf, natts as _);
    for _ in 0..natts {
        /* use the same format for all columns */
        pq_sendint16(buf, copy_format as _);
    }

    pq_endmessage(buf);
}

/*
 * send_copy_end sends the CopyDone message to end a
 * COPY .. TO STDOUT.
 */
unsafe fn send_copy_end() {
    pq_putemptymessage('c' as _);
}

/*
 * send_copy_data sends a CopyData message containing the given buffer.
 */
unsafe fn send_copy_data(data: &[u8]) {
    let buf = makeStringInfo();

    pq_beginmessage(buf, 'd' as _);
    pq_sendbytes(buf, data.as_ptr() as _, data.len() as _);
    pq_endmessage(buf);
}
