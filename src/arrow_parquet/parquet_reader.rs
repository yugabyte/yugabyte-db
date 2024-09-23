use arrow::array::RecordBatch;
use futures::StreamExt;
use parquet::arrow::async_reader::{ParquetObjectReader, ParquetRecordBatchStream};
use pgrx::{
    check_for_interrupts,
    pg_sys::{
        fmgr_info, getTypeBinaryOutputInfo, varlena, Datum, FmgrInfo, InvalidOid, SendFunctionCall,
    },
    vardata_any, varsize_any_exhdr, void_mut_ptr, AllocatedByPostgres, PgBox, PgTupleDesc,
};
use url::Url;

use crate::{
    arrow_parquet::arrow_to_pg::to_pg_datum,
    pgrx_utils::collect_valid_attributes,
    type_compat::{geometry::reset_postgis_context, map::reset_map_context},
};

use super::{
    arrow_to_pg::{collect_arrow_to_pg_attribute_contexts, ArrowToPgAttributeContext},
    schema_parser::ensure_arrow_schema_match_tupledesc,
    uri_utils::{parquet_reader_from_uri, PG_BACKEND_TOKIO_RUNTIME},
};

pub(crate) struct ParquetReaderContext {
    buffer: Vec<u8>,
    offset: usize,
    started: bool,
    finished: bool,
    parquet_reader: ParquetRecordBatchStream<ParquetObjectReader>,
    attribute_contexts: Vec<ArrowToPgAttributeContext>,
    binary_out_funcs: Vec<PgBox<FmgrInfo>>,
}

impl ParquetReaderContext {
    pub(crate) fn new(uri: Url, tupledesc: &PgTupleDesc) -> Self {
        // Postgis and Map contexts are used throughout reading the parquet file.
        // We need to reset them to avoid reading the stale data. (e.g. extension could be dropped)
        reset_postgis_context();
        reset_map_context();

        let parquet_reader = parquet_reader_from_uri(&uri);

        let schema = parquet_reader.schema();
        ensure_arrow_schema_match_tupledesc(schema.clone(), tupledesc);

        let binary_out_funcs = Self::collect_binary_out_funcs(tupledesc);

        let attribute_contexts = collect_arrow_to_pg_attribute_contexts(tupledesc, &schema.fields);

        ParquetReaderContext {
            buffer: Vec::new(),
            offset: 0,
            attribute_contexts,
            parquet_reader,
            binary_out_funcs,
            started: false,
            finished: false,
        }
    }

    fn collect_binary_out_funcs(
        tupledesc: &PgTupleDesc,
    ) -> Vec<PgBox<FmgrInfo, AllocatedByPostgres>> {
        unsafe {
            let mut binary_out_funcs = vec![];

            let include_generated_columns = false;
            let attributes = collect_valid_attributes(tupledesc, include_generated_columns);

            for att in attributes.iter() {
                let typoid = att.type_oid();

                let mut send_func_oid = InvalidOid;
                let mut is_varlena = false;
                getTypeBinaryOutputInfo(typoid.value(), &mut send_func_oid, &mut is_varlena);

                let arg_fninfo = PgBox::<FmgrInfo>::alloc0().into_pg_boxed();
                fmgr_info(send_func_oid, arg_fninfo.as_ptr());

                binary_out_funcs.push(arg_fninfo);
            }

            binary_out_funcs
        }
    }

    fn record_batch_to_tuple_datums(
        record_batch: RecordBatch,
        attribute_contexts: &[ArrowToPgAttributeContext],
    ) -> Vec<Option<Datum>> {
        let mut datums = vec![];

        for attribute_context in attribute_contexts {
            let name = attribute_context.name();

            let column = record_batch
                .column_by_name(name)
                .unwrap_or_else(|| panic!("column {} not found", name));

            let datum = to_pg_datum(column.to_data(), attribute_context);

            datums.push(datum);
        }

        datums
    }

    pub(crate) fn read_parquet(&mut self) -> bool {
        if self.finished {
            return false;
        }

        if !self.started {
            // starts PG copy protocol
            self.copy_start();
        }

        let natts = self.attribute_contexts.len() as i16;

        // read a record batch from the parquet file. Record batch will contain
        // DEFAULT_BATCH_SIZE rows as we configured in the parquet reader.
        let record_batch = PG_BACKEND_TOKIO_RUNTIME.block_on(self.parquet_reader.next());

        if let Some(batch_result) = record_batch {
            let record_batch =
                batch_result.unwrap_or_else(|e| panic!("failed to read record batch: {}", e));

            let num_rows = record_batch.num_rows();

            for i in 0..num_rows {
                check_for_interrupts!();

                // slice the record batch to get the next row
                let record_batch = record_batch.slice(i, 1);

                /* 2 bytes: per-tuple header */
                let attnum_len_bytes = natts.to_be_bytes();
                self.buffer.extend_from_slice(&attnum_len_bytes);

                // convert the columnar arrays in record batch to tuple datums
                let tuple_datums =
                    Self::record_batch_to_tuple_datums(record_batch, &self.attribute_contexts);

                // write the tuple datums to the ParquetReader's internal buffer in PG copy format
                for (datum, out_func) in tuple_datums.into_iter().zip(self.binary_out_funcs.iter())
                {
                    if let Some(datum) = datum {
                        unsafe {
                            let datum_bytes: *mut varlena =
                                SendFunctionCall(out_func.as_ptr(), datum);

                            /* 4 bytes: attribute's data size */
                            let data_size = varsize_any_exhdr(datum_bytes);
                            let data_size_bytes = (data_size as i32).to_be_bytes();
                            self.buffer.extend_from_slice(&data_size_bytes);

                            /* variable bytes: attribute's data */
                            let data = vardata_any(datum_bytes) as *const u8;
                            let data_bytes = std::slice::from_raw_parts(data, data_size);
                            self.buffer.extend_from_slice(data_bytes);
                        };
                    } else {
                        /* 4 bytes: null */
                        let null_value = -1_i32;
                        let null_value_bytes = null_value.to_be_bytes();
                        self.buffer.extend_from_slice(&null_value_bytes);
                    }
                }
            }
        } else {
            // finish PG copy protocol
            self.copy_finish();
        }

        true
    }

    fn copy_start(&mut self) {
        self.started = true;

        /* Binary signature */
        let signature_bytes = b"\x50\x47\x43\x4f\x50\x59\x0a\xff\x0d\x0a\x00";
        self.buffer.extend_from_slice(signature_bytes);

        /* Flags field */
        let flags = 0_i32;
        let flags_bytes = flags.to_be_bytes();
        self.buffer.extend_from_slice(&flags_bytes);

        /* No header extension */
        let header_ext_len = 0_i32;
        let header_ext_len_bytes = header_ext_len.to_be_bytes();
        self.buffer.extend_from_slice(&header_ext_len_bytes);
    }

    fn copy_finish(&mut self) {
        self.finished = true;

        /* trailer */
        let trailer_len = -1_i16;
        let trailer_len_bytes = trailer_len.to_be_bytes();
        self.buffer.extend_from_slice(&trailer_len_bytes);
    }

    // not_yet_copied_bytes returns the number of the bytes that are not yet consumed
    // in the ParquetReader's internal buffer.
    pub(crate) fn not_yet_copied_bytes(&self) -> usize {
        self.buffer.len() - self.offset
    }

    // reset_buffer resets the ParquetReader's internal buffer.
    pub(crate) fn reset_buffer(&mut self) {
        self.buffer.clear();
        self.offset = 0;
    }

    // copy_to_outbuf copies the next len bytes from the ParquetReader's internal buffer to the
    // outbuf. It also updates the offset of the ParquetReader's internal buffer to keep track of
    // the consumed bytes.
    pub(crate) fn copy_to_outbuf(&mut self, len: usize, outbuf: void_mut_ptr) {
        unsafe {
            std::ptr::copy_nonoverlapping(
                self.buffer.as_ptr().add(self.offset),
                outbuf as *mut u8,
                len,
            );

            self.offset += len;
        };
    }
}
