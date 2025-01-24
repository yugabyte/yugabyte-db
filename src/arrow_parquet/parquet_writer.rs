use std::sync::Arc;

use arrow::array::RecordBatch;
use arrow_schema::SchemaRef;
use parquet::{
    arrow::{async_writer::ParquetObjectWriter, AsyncArrowWriter},
    file::properties::{EnabledStatistics, WriterProperties},
    format::KeyValue,
};
use pgrx::{heap_tuple::PgHeapTuple, AllocatedByRust, PgTupleDesc};
use url::Url;

use crate::{
    arrow_parquet::{
        compression::{PgParquetCompression, PgParquetCompressionWithLevel},
        pg_to_arrow::context::collect_pg_to_arrow_attribute_contexts,
        schema_parser::{
            parquet_schema_string_from_attributes, parse_arrow_schema_from_attributes,
        },
        uri_utils::parquet_writer_from_uri,
    },
    pgrx_utils::{collect_attributes_for, CollectAttributesFor},
    type_compat::{
        geometry::{geoparquet_metadata_json_from_tupledesc, reset_postgis_context},
        map::reset_map_context,
    },
    PG_BACKEND_TOKIO_RUNTIME,
};

use super::pg_to_arrow::{context::PgToArrowAttributeContext, to_arrow_array};

pub(crate) const DEFAULT_ROW_GROUP_SIZE: i64 = 122880;
pub(crate) const DEFAULT_ROW_GROUP_SIZE_BYTES: i64 = DEFAULT_ROW_GROUP_SIZE * 1024;

pub(crate) struct ParquetWriterContext {
    parquet_writer: AsyncArrowWriter<ParquetObjectWriter>,
    schema: SchemaRef,
    attribute_contexts: Vec<PgToArrowAttributeContext>,
}

impl ParquetWriterContext {
    pub(crate) fn new(
        uri: Url,
        compression: PgParquetCompression,
        compression_level: i32,
        tupledesc: &PgTupleDesc,
    ) -> ParquetWriterContext {
        // Postgis and Map contexts are used throughout writing the parquet file.
        // We need to reset them to avoid reading the stale data. (e.g. extension could be dropped)
        reset_postgis_context();
        reset_map_context();

        let attributes = collect_attributes_for(CollectAttributesFor::CopyTo, tupledesc);

        pgrx::debug2!(
            "schema for tuples: {}",
            parquet_schema_string_from_attributes(&attributes)
        );

        let schema = parse_arrow_schema_from_attributes(&attributes);
        let schema = Arc::new(schema);

        let writer_props = Self::writer_props(tupledesc, compression, compression_level);

        let parquet_writer = parquet_writer_from_uri(&uri, schema.clone(), writer_props);

        let attribute_contexts =
            collect_pg_to_arrow_attribute_contexts(&attributes, &schema.fields);

        ParquetWriterContext {
            parquet_writer,
            schema,
            attribute_contexts,
        }
    }

    fn writer_props(
        tupledesc: &PgTupleDesc,
        compression: PgParquetCompression,
        compression_level: i32,
    ) -> WriterProperties {
        let compression = PgParquetCompressionWithLevel {
            compression,
            compression_level,
        };

        let mut writer_props_builder = WriterProperties::builder()
            .set_statistics_enabled(EnabledStatistics::Page)
            .set_compression(compression.into())
            .set_created_by("pg_parquet".to_string());

        let geometry_columns_metadata_value = geoparquet_metadata_json_from_tupledesc(tupledesc);

        if geometry_columns_metadata_value.is_some() {
            let key_value_metadata = KeyValue::new("geo".into(), geometry_columns_metadata_value);

            writer_props_builder =
                writer_props_builder.set_key_value_metadata(Some(vec![key_value_metadata]));
        }

        writer_props_builder.build()
    }

    pub(crate) fn write_new_row_group(
        &mut self,
        tuples: Vec<Option<PgHeapTuple<AllocatedByRust>>>,
    ) {
        let record_batch =
            Self::pg_tuples_to_record_batch(tuples, &self.attribute_contexts, self.schema.clone());

        let parquet_writer = &mut self.parquet_writer;

        PG_BACKEND_TOKIO_RUNTIME
            .block_on(parquet_writer.write(&record_batch))
            .unwrap_or_else(|e| panic!("failed to write record batch: {}", e));

        PG_BACKEND_TOKIO_RUNTIME
            .block_on(parquet_writer.flush())
            .unwrap_or_else(|e| panic!("failed to flush record batch: {}", e));
    }

    fn pg_tuples_to_record_batch(
        tuples: Vec<Option<PgHeapTuple<AllocatedByRust>>>,
        attribute_contexts: &[PgToArrowAttributeContext],
        schema: SchemaRef,
    ) -> RecordBatch {
        let mut attribute_arrays = vec![];

        for attribute_context in attribute_contexts {
            let attribute_array = to_arrow_array(&tuples, attribute_context);

            attribute_arrays.push(attribute_array);
        }

        RecordBatch::try_new(schema, attribute_arrays).expect("Expected record batch")
    }
}

impl Drop for ParquetWriterContext {
    fn drop(&mut self) {
        PG_BACKEND_TOKIO_RUNTIME
            .block_on(self.parquet_writer.finish())
            .unwrap_or_else(|e| {
                panic!("failed to close parquet writer: {}", e);
            });
    }
}
