use parquet::data_type::ByteArrayType;
use parquet::data_type::Int32Type;
use parquet::file::reader::FileReader;
use parquet::file::reader::SerializedFileReader;
use parquet::file::writer::SerializedFileWriter;
use parquet::record::RowAccessor;
use parquet::record::{RecordReader, RecordWriter};
use parquet_derive::{ParquetRecordReader, ParquetRecordWriter};

use pg_sys::{
    BOOLARRAYOID, BOOLOID, BPCHARARRAYOID, BPCHAROID, CHARARRAYOID, CHAROID, DATEARRAYOID, DATEOID,
    FLOAT4ARRAYOID, FLOAT4OID, FLOAT8ARRAYOID, FLOAT8OID, INT2ARRAYOID, INT2OID, INT4ARRAYOID,
    INT4OID, INT8ARRAYOID, INT8OID, INTERVALARRAYOID, INTERVALOID, NUMERICARRAYOID, NUMERICOID,
    RECORDARRAYOID, RECORDOID, TEXTARRAYOID, TEXTOID, TIMEARRAYOID, TIMEOID, TIMESTAMPARRAYOID,
    TIMESTAMPOID, TIMESTAMPTZARRAYOID, TIMESTAMPTZOID, TIMETZARRAYOID, TIMETZOID, VARCHARARRAYOID,
    VARCHAROID,
};
use pgrx::prelude::*;

pgrx::pg_module_magic!();

// parquet serializable Person struct with id and name via parquet::record API
#[derive(Debug, Clone, ParquetRecordReader, ParquetRecordWriter)]
struct Person {
    id: i32,
    name: String,
}

struct Person2 {
    id: i32,
    name: String,
}

impl RecordReader<Person2> for Vec<Person2> {
    fn read_from_row_group(
        &mut self,
        row_group_reader: &mut dyn parquet::file::reader::RowGroupReader,
        num_records: usize,
    ) -> Result<(), parquet::errors::ParquetError> {
        assert!(num_records == 1);

        let row = row_group_reader
            .get_row_iter(None)
            .unwrap()
            .next()
            .unwrap()
            .unwrap();

        let id = row.get_int(0).unwrap();
        let name = row.get_bytes(1).unwrap().as_utf8().unwrap().to_string();
        self.push(Person2 { id, name });

        Ok(())
    }
}

impl RecordWriter<Person2> for &[Person2] {
    fn write_to_row_group<W: std::io::Write + Send>(
        &self,
        row_group_writer: &mut parquet::file::writer::SerializedRowGroupWriter<W>,
    ) -> Result<(), parquet::errors::ParquetError> {
        assert!(self.len() == 1);

        let person = &self[0];

        let schema = Self::schema(&self).unwrap();
        let fields = schema.get_fields();

        fields.iter().for_each(|field| {
            let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

            let field_name = field.name();
            match field_name {
                "id" => {
                    column_writer
                        .typed::<Int32Type>()
                        .write_batch(&[person.id], None, None)
                        .unwrap();
                }
                "name" => {
                    column_writer
                        .typed::<ByteArrayType>()
                        .write_batch(&[person.name.as_bytes().into()], None, None)
                        .unwrap();
                }
                _ => panic!("unexpected field name"),
            }

            column_writer.close().unwrap();
        });

        Ok(())
    }

    fn schema(&self) -> Result<parquet::schema::types::TypePtr, parquet::errors::ParquetError> {
        assert!(self.len() == 1);

        let id_type =
            parquet::schema::types::Type::primitive_type_builder("id", parquet::basic::Type::INT32)
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

        let name_type = parquet::schema::types::Type::primitive_type_builder(
            "name",
            parquet::basic::Type::BYTE_ARRAY,
        )
        .with_repetition(parquet::basic::Repetition::REQUIRED)
        .build()
        .unwrap();

        let person_group_type = parquet::schema::types::Type::group_type_builder("person")
            .with_fields(vec![id_type.into(), name_type.into()])
            .build()
            .unwrap();

        Ok(person_group_type.into())
    }
}

//// nested struct
#[derive(Debug, Clone, Default)]
struct Cat {
    id: i32,
    name: String,
}

impl RecordReader<Cat> for Vec<Cat> {
    fn read_from_row_group(
        &mut self,
        row_group_reader: &mut dyn parquet::file::reader::RowGroupReader,
        num_records: usize,
    ) -> Result<(), parquet::errors::ParquetError> {
        assert!(num_records == 1);

        let row = row_group_reader
            .get_row_iter(None)
            .unwrap()
            .next()
            .unwrap()
            .unwrap();

        let id = row.get_int(0).unwrap();
        let name = row.get_bytes(1).unwrap().as_utf8().unwrap().to_string();

        self.push(Cat { id, name });

        Ok(())
    }
}

impl RecordWriter<Cat> for &[Cat] {
    fn write_to_row_group<W: std::io::Write + Send>(
        &self,
        row_group_writer: &mut parquet::file::writer::SerializedRowGroupWriter<W>,
    ) -> Result<(), parquet::errors::ParquetError> {
        assert!(self.len() == 1);

        let cat = &self[0];

        let schema = Self::schema(&self).unwrap();
        let fields = schema.get_fields();

        fields.iter().for_each(|field| {
            let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

            let field_name = field.name();
            match field_name {
                "id" => {
                    column_writer
                        .typed::<Int32Type>()
                        .write_batch(&[cat.id], None, None)
                        .unwrap();
                }
                "name" => {
                    column_writer
                        .typed::<ByteArrayType>()
                        .write_batch(&[cat.name.as_bytes().into()], None, None)
                        .unwrap();
                }
                _ => panic!("unexpected field name"),
            }

            column_writer.close().unwrap();
        });

        Ok(())
    }

    fn schema(&self) -> Result<parquet::schema::types::TypePtr, parquet::errors::ParquetError> {
        assert!(self.len() == 1);

        let id_type =
            parquet::schema::types::Type::primitive_type_builder("id", parquet::basic::Type::INT32)
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

        let name_type = parquet::schema::types::Type::primitive_type_builder(
            "name",
            parquet::basic::Type::BYTE_ARRAY,
        )
        .with_repetition(parquet::basic::Repetition::REQUIRED)
        .build()
        .unwrap();

        let cat_group_type = parquet::schema::types::Type::group_type_builder("cat")
            .with_fields(vec![id_type.into(), name_type.into()])
            .build()
            .unwrap();

        Ok(cat_group_type.into())
    }
}

struct NestedPerson {
    id: i32,
    name: String,
    cat: Option<Cat>,
}

impl RecordReader<NestedPerson> for Vec<NestedPerson> {
    fn read_from_row_group(
        &mut self,
        row_group_reader: &mut dyn parquet::file::reader::RowGroupReader,
        num_records: usize,
    ) -> Result<(), parquet::errors::ParquetError> {
        assert!(num_records == 1);

        let row = row_group_reader
            .get_row_iter(None)
            .unwrap()
            .next()
            .unwrap()
            .unwrap();

        let id = row.get_int(0).unwrap();
        let name = row.get_bytes(1).unwrap().as_utf8().unwrap().to_string();
        let cat = if let Ok(cat_row) = row.get_group(2) {
            let cat_id = cat_row.get_int(0).unwrap();
            let cat_name = cat_row.get_bytes(1).unwrap().as_utf8().unwrap().to_string();
            let cat = Some(Cat {
                id: cat_id,
                name: cat_name,
            });
            cat
        } else {
            None
        };

        self.push(NestedPerson { id, name, cat });

        Ok(())
    }
}

impl RecordWriter<NestedPerson> for &[NestedPerson] {
    fn write_to_row_group<W: std::io::Write + Send>(
        &self,
        row_group_writer: &mut parquet::file::writer::SerializedRowGroupWriter<W>,
    ) -> Result<(), parquet::errors::ParquetError> {
        assert!(self.len() == 1);

        let person = &self[0];

        let schema = Self::schema(&self).unwrap();
        let fields = schema.get_fields();

        fields.iter().for_each(|field| {
            let field_name = field.name();
            match field_name {
                "id" => {
                    let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                    column_writer
                        .typed::<Int32Type>()
                        .write_batch(&[person.id], None, None)
                        .unwrap();

                    column_writer.close().unwrap();
                }
                "name" => {
                    let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                    column_writer
                        .typed::<ByteArrayType>()
                        .write_batch(&[person.name.as_bytes().into()], None, None)
                        .unwrap();

                    column_writer.close().unwrap();
                }
                "cat" => {
                    if let Some(cat) = &person.cat {
                        let cat_fields = field.get_fields();
                        cat_fields.iter().for_each(|cat_field| {
                            let cat_field_name = cat_field.name();
                            match cat_field_name {
                                "id" => {
                                    let mut column_writer =
                                        row_group_writer.next_column().unwrap().unwrap();

                                    column_writer
                                        .typed::<Int32Type>()
                                        .write_batch(&[cat.id], Some(&[1]), None)
                                        .unwrap();

                                    column_writer.close().unwrap();
                                }
                                "name" => {
                                    let mut column_writer =
                                        row_group_writer.next_column().unwrap().unwrap();

                                    column_writer
                                        .typed::<ByteArrayType>()
                                        .write_batch(
                                            &[cat.name.as_bytes().into()],
                                            Some(&[1]),
                                            None,
                                        )
                                        .unwrap();

                                    column_writer.close().unwrap();
                                }
                                _ => {
                                    panic!("unexpected field name");
                                }
                            }
                        });
                    }
                }
                _ => {
                    panic!("unexpected field name");
                }
            }
        });

        Ok(())
    }

    fn schema(&self) -> Result<parquet::schema::types::TypePtr, parquet::errors::ParquetError> {
        assert!(self.len() == 1);

        let id_type =
            parquet::schema::types::Type::primitive_type_builder("id", parquet::basic::Type::INT32)
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

        let name_type = parquet::schema::types::Type::primitive_type_builder(
            "name",
            parquet::basic::Type::BYTE_ARRAY,
        )
        .with_repetition(parquet::basic::Repetition::REQUIRED)
        .build()
        .unwrap();

        let cat_group_type = parquet::schema::types::Type::group_type_builder("cat")
            .with_fields(vec![
                parquet::schema::types::Type::primitive_type_builder(
                    "id",
                    parquet::basic::Type::INT32,
                )
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap()
                .into(),
                parquet::schema::types::Type::primitive_type_builder(
                    "name",
                    parquet::basic::Type::BYTE_ARRAY,
                )
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap()
                .into(),
            ])
            .with_repetition(parquet::basic::Repetition::OPTIONAL)
            .build()
            .unwrap();

        let person_group_type = parquet::schema::types::Type::group_type_builder("person")
            .with_fields(vec![
                id_type.into(),
                name_type.into(),
                cat_group_type.into(),
            ])
            .build()
            .unwrap();

        Ok(person_group_type.into())
    }
}

#[pg_schema]
mod pgparquet {
    use parquet::data_type::{
        ByteArray, DoubleType, FixedLenByteArray, FixedLenByteArrayType, FloatType, Int64Type,
    };
    use pg_sys::{JSONOID, UUIDARRAYOID, UUIDOID};
    use pgrx::{direct_function_call, Json, Uuid};

    use super::*;

    fn serialize_record(tuple: PgHeapTuple<'static, AllocatedByRust>, tuple_name: Option<&str>) {
        if let Some(tuple_name) = tuple_name {
            pgrx::info!("\"{}\": ", tuple_name);
        }
        pgrx::info!("{{");

        let attributes_len = tuple.len();
        for (idx, (_, attribute)) in tuple.attributes().enumerate() {
            let attribute_name = attribute.name();
            let attribute_oid = attribute.type_oid().value();

            let is_attribute_composite = unsafe { pg_sys::type_is_rowtype(attribute_oid) };

            if is_attribute_composite {
                let attribute_val = tuple
                    .get_by_name::<PgHeapTuple<'_, AllocatedByRust>>(attribute_name)
                    .unwrap()
                    .unwrap();
                serialize_record(attribute_val, Some(attribute_name));
            } else {
                let attribute_val = tuple
                    .get_by_name::<pgrx::AnyElement>(attribute_name)
                    .unwrap()
                    .unwrap();
                serialize(attribute_val, Some(attribute_name));
            }

            if idx < attributes_len - 1 {
                pgrx::info!(",");
            }
        }
    }

    #[pg_extern]
    fn serialize(elem: pgrx::AnyElement, elem_name: default!(Option<&str>, "NULL")) {
        if let Some(elem_name) = elem_name {
            pgrx::info!("\"{}\": ", elem_name);
        }

        match elem.oid() {
            FLOAT4OID => {
                let value = unsafe {
                    f32::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };

                let float4_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::FLOAT,
                )
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![float4_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<FloatType>()
                    .write_batch(&[value], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            FLOAT8OID => {
                let value = unsafe {
                    f64::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };

                let float8_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::DOUBLE,
                )
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![float8_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<DoubleType>()
                    .write_batch(&[value], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            INT2OID => {
                let value = unsafe {
                    i16::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };

                let int2_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::INT32,
                )
                .with_logical_type(Some(parquet::basic::LogicalType::Integer {
                    bit_width: 16,
                    is_signed: true,
                }))
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![int2_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<Int32Type>()
                    .write_batch(&[value as i32], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            INT4OID => {
                let value = unsafe {
                    i32::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };

                let int4_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::INT32,
                )
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![int4_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<Int32Type>()
                    .write_batch(&[value], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            INT8OID => {
                let value = unsafe {
                    i64::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };

                let int8_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::INT64,
                )
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![int8_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<Int64Type>()
                    .write_batch(&[value], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            NUMERICOID => {
                // todo: totally wrong
                let value = unsafe {
                    AnyNumeric::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                let numeric_as_bytes: Vec<u8> = unsafe {
                    direct_function_call(pg_sys::numeric_send, &[value.clone().into_datum()])
                        .unwrap()
                };
                let numeric_as_bytes: ByteArray = numeric_as_bytes.as_slice().into();

                // find scale and precision
                let numeric_as_str: &core::ffi::CStr = unsafe {
                    direct_function_call(pg_sys::numeric_out, &[value.into_datum()]).unwrap()
                };
                let numeric_as_str = numeric_as_str.to_str().unwrap();
                let numeric_split: Vec<&str> = numeric_as_str.split('.').collect();
                let first_part_len = numeric_split[0].len() as i32;
                let second_part_len = if numeric_split.len() > 1 {
                    numeric_split[1].len() as i32
                } else {
                    0
                };
                let precision = first_part_len + second_part_len;
                let scale = second_part_len;

                let numeric_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::BYTE_ARRAY,
                )
                .with_logical_type(Some(parquet::basic::LogicalType::Decimal {
                    precision,
                    scale,
                }))
                .with_precision(precision)
                .with_scale(scale)
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![numeric_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<ByteArrayType>()
                    .write_batch(&[numeric_as_bytes], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            DATEOID => {
                let value = unsafe {
                    Date::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };

                // PG epoch is (2000-01-01). Convert it to Unix epoch (1970-01-01). +10957 days
                let value: Date = unsafe {
                    direct_function_call(
                        pg_sys::date_pli,
                        &[value.into_datum(), 10957.into_datum()],
                    )
                    .unwrap()
                };

                let date_as_bytes: Vec<u8> = unsafe {
                    direct_function_call(pg_sys::date_send, &[value.into_datum()]).unwrap()
                };
                let date_as_int = i32::from_be_bytes(date_as_bytes[0..4].try_into().unwrap());

                let date_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::INT32,
                )
                .with_logical_type(Some(parquet::basic::LogicalType::Date))
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![date_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<Int32Type>()
                    .write_batch(&[date_as_int], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            TIMESTAMPOID => {
                let value =
                    unsafe { Timestamp::from_polymorphic_datum(elem.datum(), false, elem.oid()) }
                        .unwrap();

                // PG epoch is (2000-01-01). Convert it to Unix epoch (1970-01-01). +10957 days
                let adjustment_interval = Interval::from_days(10957);
                let value: Timestamp = unsafe {
                    direct_function_call(
                        pg_sys::timestamp_pl_interval,
                        &[value.into_datum(), adjustment_interval.into_datum()],
                    )
                    .unwrap()
                };

                let timestamp_as_bytes: Vec<u8> = unsafe {
                    direct_function_call(pg_sys::time_send, &[value.into_datum()]).unwrap()
                };
                let timestamp_val =
                    i64::from_be_bytes(timestamp_as_bytes[0..8].try_into().unwrap());

                let timestamp_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::INT64,
                )
                .with_logical_type(Some(parquet::basic::LogicalType::Timestamp {
                    is_adjusted_to_u_t_c: false,
                    unit: parquet::basic::TimeUnit::MICROS(parquet::format::MicroSeconds {}),
                }))
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![timestamp_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<Int64Type>()
                    .write_batch(&[timestamp_val], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            TIMESTAMPTZOID => {
                let value = unsafe {
                    TimestampWithTimeZone::from_polymorphic_datum(elem.datum(), false, elem.oid())
                }
                .unwrap();

                // PG epoch is (2000-01-01). Convert it to Unix epoch (1970-01-01). +10957 days
                let adjustment_interval = Interval::from_days(10957);
                let value: TimestampWithTimeZone = unsafe {
                    direct_function_call(
                        pg_sys::timestamptz_pl_interval,
                        &[value.into_datum(), adjustment_interval.into_datum()],
                    )
                    .unwrap()
                };

                let timestamp_as_bytes: Vec<u8> = unsafe {
                    direct_function_call(pg_sys::timestamptz_send, &[value.into_datum()]).unwrap()
                };
                let timestamp_val =
                    i64::from_be_bytes(timestamp_as_bytes[0..8].try_into().unwrap());

                let timestamp_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::INT64,
                )
                .with_logical_type(Some(parquet::basic::LogicalType::Timestamp {
                    is_adjusted_to_u_t_c: true,
                    unit: parquet::basic::TimeUnit::MICROS(parquet::format::MicroSeconds {}),
                }))
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![timestamp_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<Int64Type>()
                    .write_batch(&[timestamp_val], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            TIMEOID => {
                let value =
                    unsafe { Time::from_polymorphic_datum(elem.datum(), false, elem.oid()) }
                        .unwrap();

                let time_as_bytes: Vec<u8> = unsafe {
                    direct_function_call(pg_sys::time_send, &[value.into_datum()]).unwrap()
                };
                let time_val = i64::from_be_bytes(time_as_bytes[0..8].try_into().unwrap());

                let time_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::INT64,
                )
                .with_logical_type(Some(parquet::basic::LogicalType::Time {
                    is_adjusted_to_u_t_c: false,
                    unit: parquet::basic::TimeUnit::MICROS(parquet::format::MicroSeconds {}),
                }))
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![time_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<Int64Type>()
                    .write_batch(&[time_val], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            TIMETZOID => {
                let value = unsafe {
                    TimeWithTimeZone::from_polymorphic_datum(elem.datum(), false, elem.oid())
                }
                .unwrap();

                // extract timezone as seconds
                let timezone_as_secs: AnyNumeric = unsafe {
                    direct_function_call(
                        pg_sys::extract_timetz,
                        &["timezone".into_datum(), value.into_datum()],
                    )
                }
                .unwrap();

                // adjust timezone
                let timezone_as_secs: f64 = timezone_as_secs.try_into().unwrap();
                let timezone_val = Interval::from_seconds(timezone_as_secs);
                let value: TimeWithTimeZone = unsafe {
                    direct_function_call(
                        pg_sys::timetz_pl_interval,
                        &[value.into_datum(), timezone_val.into_datum()],
                    )
                    .unwrap()
                };

                let time_as_bytes: Vec<u8> = unsafe {
                    direct_function_call(pg_sys::timetz_send, &[value.into_datum()]).unwrap()
                };
                let time_val = i64::from_be_bytes(time_as_bytes[0..8].try_into().unwrap());

                let time_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::INT64,
                )
                .with_logical_type(Some(parquet::basic::LogicalType::Time {
                    is_adjusted_to_u_t_c: true,
                    unit: parquet::basic::TimeUnit::MICROS(parquet::format::MicroSeconds {}),
                }))
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![time_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<Int64Type>()
                    .write_batch(&[time_val], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            INTERVALOID => {
                let value =
                    unsafe { Interval::from_polymorphic_datum(elem.datum(), false, elem.oid()) }
                        .unwrap();

                let interval_as_bytes: Vec<u8> = unsafe {
                    direct_function_call(pg_sys::interval_send, &[value.into_datum()]).unwrap()
                };

                // first 8 bytes: time in microsec
                // next 4 bytes: day
                // next 4 bytes: month
                let time_in_microsec_val =
                    i64::from_be_bytes(interval_as_bytes[0..8].try_into().unwrap());
                let day_val = i32::from_be_bytes(interval_as_bytes[8..12].try_into().unwrap());
                let month_val = i32::from_be_bytes(interval_as_bytes[12..16].try_into().unwrap());

                // Postgres interval has microsecond resolution, parquet only milliseconds
                // plus postgres doesn't overflow the seconds into the day field
                let ms_per_day = 1000 * 60 * 60 * 24;
                let millis_total = time_in_microsec_val / 1000;
                let days = millis_total / ms_per_day;
                let millis = millis_total % ms_per_day;
                let mut b = vec![0u8; 12];
                b[0..4].copy_from_slice(&i32::to_le_bytes(month_val));
                b[4..8].copy_from_slice(&i32::to_le_bytes(day_val + days as i32));
                b[8..12].copy_from_slice(&i32::to_le_bytes(millis as i32));
                let interval_as_bytes = FixedLenByteArray::from(b);

                let interval_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::FIXED_LEN_BYTE_ARRAY,
                )
                .with_length(12)
                .with_converted_type(parquet::basic::ConvertedType::INTERVAL)
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![interval_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<FixedLenByteArrayType>()
                    .write_batch(&[interval_as_bytes], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            CHAROID => {
                let value =
                    unsafe { i8::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap() };
                let value: ByteArray = vec![value as u8].into();

                let char_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::BYTE_ARRAY,
                )
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![char_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<ByteArrayType>()
                    .write_batch(&[value], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            TEXTOID | VARCHAROID | BPCHAROID => {
                let value = unsafe {
                    String::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                let value: ByteArray = value.as_bytes().into();

                let text_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::BYTE_ARRAY,
                )
                .with_logical_type(Some(parquet::basic::LogicalType::String))
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![text_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<ByteArrayType>()
                    .write_batch(&[value], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            BOOLOID => {
                let value = unsafe {
                    bool::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };

                let bool_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::INT32,
                )
                .with_logical_type(Some(parquet::basic::LogicalType::Integer {
                    bit_width: 8,
                    is_signed: true,
                }))
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![bool_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<Int32Type>()
                    .write_batch(&[value as i32], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            UUIDOID => {
                let value = unsafe {
                    Uuid::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                let uuid_as_bytes: Vec<u8> = unsafe {
                    direct_function_call(pg_sys::uuid_send, &[value.into_datum()]).unwrap()
                };
                let value: FixedLenByteArray = uuid_as_bytes.into();

                let uuid_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::FIXED_LEN_BYTE_ARRAY,
                )
                .with_length(16)
                .with_logical_type(Some(parquet::basic::LogicalType::Uuid))
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![uuid_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<FixedLenByteArrayType>()
                    .write_batch(&[value], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            JSONOID => {
                let value = unsafe {
                    Json::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                let json_as_bytes: Vec<u8> = unsafe {
                    direct_function_call(pg_sys::json_send, &[value.into_datum()]).unwrap()
                };
                let value: ByteArray = json_as_bytes.into();

                let json_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::BYTE_ARRAY,
                )
                .with_logical_type(Some(parquet::basic::LogicalType::Json))
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let row_group_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![json_type.into()])
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, row_group_type.into(), Default::default())
                        .unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                column_writer
                    .typed::<ByteArrayType>()
                    .write_batch(&[value], None, None)
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            RECORDOID => {
                let record = unsafe {
                    PgHeapTuple::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                serialize_record(record, None);
            }
            FLOAT4ARRAYOID => {
                let value = unsafe {
                    Vec::<f32>::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };

                // write to parquet as list
                let float4_type = parquet::schema::types::Type::primitive_type_builder(
                    elem_name.unwrap(),
                    parquet::basic::Type::FLOAT,
                )
                .with_repetition(parquet::basic::Repetition::REQUIRED)
                .build()
                .unwrap();

                let list_type = parquet::schema::types::Type::group_type_builder("list")
                    .with_fields(vec![float4_type.into()])
                    .with_repetition(parquet::basic::Repetition::REPEATED)
                    .with_logical_type(Some(parquet::basic::LogicalType::List))
                    .build()
                    .unwrap();

                let list_type = parquet::schema::types::Type::group_type_builder("root")
                    .with_fields(vec![list_type.into()])
                    .with_repetition(parquet::basic::Repetition::REQUIRED)
                    .build()
                    .unwrap();

                let file = std::fs::OpenOptions::new()
                    .write(true)
                    .create(true)
                    .open("/home/aykutbozkurt/Projects/pg_parquet/test.parquet")
                    .unwrap();

                let mut writer =
                    SerializedFileWriter::new(file, list_type.into(), Default::default()).unwrap();
                let mut row_group_writer = writer.next_row_group().unwrap();
                let mut column_writer = row_group_writer.next_column().unwrap().unwrap();

                let def_levels = vec![1; value.len()];
                let mut rep_levels = vec![1; value.len()];
                rep_levels[0] = 0;
                column_writer
                    .typed::<FloatType>()
                    .write_batch(&value, Some(&def_levels), Some(&rep_levels))
                    .unwrap();

                column_writer.close().unwrap();
                row_group_writer.close().unwrap();
                writer.close().unwrap();
            }
            FLOAT8ARRAYOID => {
                let value = unsafe {
                    Vec::<f64>::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            INT2ARRAYOID => {
                let value = unsafe {
                    Vec::<i16>::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            INT4ARRAYOID => {
                let value = unsafe {
                    Vec::<i32>::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            INT8ARRAYOID => {
                let value = unsafe {
                    Vec::<i64>::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            NUMERICARRAYOID => {
                let value = unsafe {
                    Vec::<AnyNumeric>::from_polymorphic_datum(elem.datum(), false, elem.oid())
                        .unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            DATEARRAYOID => {
                let value = unsafe {
                    Vec::<Date>::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            TIMESTAMPARRAYOID => {
                let value = unsafe {
                    Vec::<Timestamp>::from_polymorphic_datum(elem.datum(), false, elem.oid())
                        .unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            TIMESTAMPTZARRAYOID => {
                let value = unsafe {
                    Vec::<TimestampWithTimeZone>::from_polymorphic_datum(
                        elem.datum(),
                        false,
                        elem.oid(),
                    )
                    .unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            TIMEARRAYOID => {
                let value = unsafe {
                    Vec::<Time>::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            TIMETZARRAYOID => {
                let value = unsafe {
                    Vec::<TimeWithTimeZone>::from_polymorphic_datum(elem.datum(), false, elem.oid())
                        .unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            INTERVALARRAYOID => {
                let value = unsafe {
                    Vec::<Interval>::from_polymorphic_datum(elem.datum(), false, elem.oid())
                        .unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            CHARARRAYOID => {
                let value = unsafe {
                    Vec::<char>::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            TEXTARRAYOID => {
                let value = unsafe {
                    Vec::<String>::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            VARCHARARRAYOID => {
                let value = unsafe {
                    Vec::<String>::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            BPCHARARRAYOID => {
                let value = unsafe {
                    Vec::<Vec<u8>>::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            BOOLARRAYOID => {
                let value = unsafe {
                    Vec::<bool>::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            UUIDARRAYOID => {
                let value = unsafe {
                    Vec::<Uuid>::from_polymorphic_datum(elem.datum(), false, elem.oid()).unwrap()
                };
                pgrx::info!("{:?}", value);
            }
            RECORDARRAYOID => {
                let records = unsafe {
                    Vec::<PgHeapTuple<'_, AllocatedByRust>>::from_polymorphic_datum(
                        elem.datum(),
                        false,
                        elem.oid(),
                    )
                    .unwrap()
                };

                pgrx::info!("[");

                let records_len = records.len();
                for (idx, record) in records.into_iter().enumerate() {
                    serialize_record(record, None);

                    if idx < records_len - 1 {
                        pgrx::info!(",");
                    }
                }

                pgrx::info!("]");
            }
            _ => {
                panic!("unsupported type {}", elem.oid());
            }
        };
    }

    #[pg_extern]
    fn write_to(filename: &str, id: i32, name: &str) {
        let person = Person {
            id,
            name: name.to_string(),
        };

        let file = std::fs::OpenOptions::new()
            .write(true)
            .create(true)
            .open(filename)
            .unwrap();
        let people = vec![person];
        let schema = people.as_slice().schema().unwrap();

        let mut writer = SerializedFileWriter::new(file, schema, Default::default()).unwrap();
        let mut row_group = writer.next_row_group().unwrap();
        people
            .as_slice()
            .write_to_row_group(&mut row_group)
            .unwrap();
        row_group.close().unwrap();
        writer.close().unwrap();
    }

    #[pg_extern]
    fn write_to2(filename: &str, id: i32, name: &str) {
        let person = Person2 {
            id,
            name: name.to_string(),
        };

        let file = std::fs::OpenOptions::new()
            .write(true)
            .create(true)
            .open(filename)
            .unwrap();
        let people = vec![person];
        let schema = people.as_slice().schema().unwrap();

        let mut writer = SerializedFileWriter::new(file, schema, Default::default()).unwrap();
        let mut row_group = writer.next_row_group().unwrap();
        people
            .as_slice()
            .write_to_row_group(&mut row_group)
            .unwrap();
        row_group.close().unwrap();
        writer.close().unwrap();
    }

    #[pg_extern]
    fn write_to3(filename: &str, id: i32, name: &str, cat_id: i32, cat_name: &str) {
        let nested_person = NestedPerson {
            id,
            name: name.to_string(),
            cat: Some(Cat {
                id: cat_id,
                name: cat_name.to_string(),
            }),
        };

        let file = std::fs::OpenOptions::new()
            .write(true)
            .create(true)
            .open(filename)
            .unwrap();
        let nested_people = vec![nested_person];
        let schema = nested_people.as_slice().schema().unwrap();

        let mut writer = SerializedFileWriter::new(file, schema, Default::default()).unwrap();
        let mut row_group = writer.next_row_group().unwrap();
        nested_people
            .as_slice()
            .write_to_row_group(&mut row_group)
            .unwrap();
        row_group.close().unwrap();
        writer.close().unwrap();
    }

    #[pg_extern]
    fn read_from(filename: &str) -> TableIterator<'static, (name!(id, i32), name!(name, String))> {
        let file = std::fs::File::open(filename).unwrap();
        let reader = SerializedFileReader::new(file).unwrap();

        let mut people: Vec<Person> = Vec::new();
        let mut row_group = reader.get_row_group(0).unwrap();
        people.read_from_row_group(&mut *row_group, 1).unwrap();

        TableIterator::new(people.into_iter().map(|person| (person.id, person.name)))
    }

    #[pg_extern]
    fn read_from2(filename: &str) -> TableIterator<'static, (name!(id, i32), name!(name, String))> {
        let file = std::fs::File::open(filename).unwrap();
        let reader = SerializedFileReader::new(file).unwrap();

        let mut people: Vec<Person2> = Vec::new();
        let mut row_group = reader.get_row_group(0).unwrap();
        people.read_from_row_group(&mut *row_group, 1).unwrap();

        TableIterator::new(people.into_iter().map(|person| (person.id, person.name)))
    }

    #[pg_extern]
    fn read_from3(
        filename: &str,
    ) -> TableIterator<
        'static,
        (
            name!(id, i32),
            name!(name, String),
            name!(cat_id, i32),
            name!(cat_name, String),
        ),
    > {
        let file = std::fs::File::open(filename).unwrap();
        let reader = SerializedFileReader::new(file).unwrap();

        let mut nested_people: Vec<NestedPerson> = Vec::new();
        let mut row_group = reader.get_row_group(0).unwrap();
        nested_people
            .read_from_row_group(&mut *row_group, 1)
            .unwrap();

        TableIterator::new(nested_people.into_iter().map(|nested_person| {
            let cat = nested_person.cat.unwrap_or_default();
            (nested_person.id, nested_person.name, cat.id, cat.name)
        }))
    }
}

#[cfg(any(test, feature = "pg_test"))]
#[pg_schema]
mod tests {
    use pgrx::prelude::*;
}

/// This module is required by `cargo pgrx test` invocations.
/// It must be visible at the root of your extension crate.
#[cfg(test)]
pub mod pg_test {
    pub fn setup(_options: Vec<&str>) {
        // perform one-off initialization when the pg_test framework starts
    }

    pub fn postgresql_conf_options() -> Vec<&'static str> {
        // return any postgresql.conf settings that are required for your tests
        vec![]
    }
}
