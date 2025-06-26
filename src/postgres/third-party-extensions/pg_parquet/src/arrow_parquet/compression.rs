use std::{fmt::Display, str::FromStr};

use parquet::basic::{BrotliLevel, Compression, GzipLevel, ZstdLevel};
use url::Url;

#[repr(C)]
#[derive(Debug, Copy, Clone, PartialEq, Default)]
pub enum PgParquetCompression {
    Uncompressed,
    #[default]
    Snappy,
    Gzip,
    Lz4,
    Lz4raw,
    Brotli,
    Zstd,
}

pub(crate) fn all_supported_compressions() -> Vec<PgParquetCompression> {
    vec![
        PgParquetCompression::Uncompressed,
        PgParquetCompression::Snappy,
        PgParquetCompression::Gzip,
        PgParquetCompression::Lz4,
        PgParquetCompression::Lz4raw,
        PgParquetCompression::Brotli,
        PgParquetCompression::Zstd,
    ]
}

impl PgParquetCompression {
    pub(crate) fn default_compression_level(&self) -> Option<i32> {
        match self {
            PgParquetCompression::Zstd => Some(ZstdLevel::default().compression_level()),
            PgParquetCompression::Gzip => Some(GzipLevel::default().compression_level() as i32),
            PgParquetCompression::Brotli => Some(BrotliLevel::default().compression_level() as i32),
            _ => None,
        }
    }

    pub(crate) fn ensure_compression_level(&self, compression_level: i32) {
        if compression_level != INVALID_COMPRESSION_LEVEL {
            match self {
                PgParquetCompression::Zstd => {
                    ZstdLevel::try_new(compression_level).unwrap_or_else(|e| panic!("{}", e));
                }
                PgParquetCompression::Gzip => {
                    GzipLevel::try_new(compression_level as u32)
                        .unwrap_or_else(|e| panic!("{}", e));
                }
                PgParquetCompression::Brotli => {
                    BrotliLevel::try_new(compression_level as u32)
                        .unwrap_or_else(|e| panic!("{}", e));
                }
                _ => panic!(
                    "compression level is not supported for \"{}\" compression",
                    self
                ),
            }
        }
    }
}

pub(crate) const INVALID_COMPRESSION_LEVEL: i32 = -1;

pub(crate) struct PgParquetCompressionWithLevel {
    pub(crate) compression: PgParquetCompression,
    pub(crate) compression_level: i32,
}

impl From<PgParquetCompressionWithLevel> for Compression {
    fn from(value: PgParquetCompressionWithLevel) -> Self {
        let PgParquetCompressionWithLevel {
            compression,
            compression_level,
        } = value;

        compression.ensure_compression_level(compression_level);

        match compression {
            PgParquetCompression::Uncompressed => Compression::UNCOMPRESSED,
            PgParquetCompression::Snappy => Compression::SNAPPY,
            PgParquetCompression::Gzip => Compression::GZIP(
                GzipLevel::try_new(compression_level as u32).expect("invalid gzip level"),
            ),
            PgParquetCompression::Lz4 => Compression::LZ4,
            PgParquetCompression::Lz4raw => Compression::LZ4_RAW,
            PgParquetCompression::Brotli => Compression::BROTLI(
                BrotliLevel::try_new(compression_level as u32).expect("invalid brotli level"),
            ),
            PgParquetCompression::Zstd => Compression::ZSTD(
                ZstdLevel::try_new(compression_level).expect("invalid zstd level"),
            ),
        }
    }
}

impl Display for PgParquetCompression {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            PgParquetCompression::Uncompressed => write!(f, "uncompressed"),
            PgParquetCompression::Snappy => write!(f, "snappy"),
            PgParquetCompression::Gzip => write!(f, "gzip"),
            PgParquetCompression::Lz4 => write!(f, "lz4"),
            PgParquetCompression::Lz4raw => write!(f, "lz4_raw"),
            PgParquetCompression::Brotli => write!(f, "brotli"),
            PgParquetCompression::Zstd => write!(f, "zstd"),
        }
    }
}

impl FromStr for PgParquetCompression {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let s = s.to_string();

        if s == PgParquetCompression::Uncompressed.to_string() {
            Ok(PgParquetCompression::Uncompressed)
        } else if s == PgParquetCompression::Snappy.to_string() {
            Ok(PgParquetCompression::Snappy)
        } else if s == PgParquetCompression::Gzip.to_string() {
            Ok(PgParquetCompression::Gzip)
        } else if s == PgParquetCompression::Lz4.to_string() {
            Ok(PgParquetCompression::Lz4)
        } else if s == PgParquetCompression::Lz4raw.to_string() {
            Ok(PgParquetCompression::Lz4raw)
        } else if s == PgParquetCompression::Brotli.to_string() {
            Ok(PgParquetCompression::Brotli)
        } else if s == PgParquetCompression::Zstd.to_string() {
            Ok(PgParquetCompression::Zstd)
        } else {
            Err(format!("uncregonized compression format: {}", s))
        }
    }
}

impl TryFrom<Url> for PgParquetCompression {
    type Error = String;

    fn try_from(value: Url) -> Result<Self, Self::Error> {
        let path = value.path();

        if path.ends_with(".parquet") || path.ends_with(".parquet.snappy") {
            Ok(PgParquetCompression::Snappy)
        } else if path.ends_with(".parquet.gz") {
            Ok(PgParquetCompression::Gzip)
        } else if path.ends_with(".parquet.lz4") {
            Ok(PgParquetCompression::Lz4)
        } else if path.ends_with(".parquet.br") {
            Ok(PgParquetCompression::Brotli)
        } else if path.ends_with(".parquet.zst") {
            Ok(PgParquetCompression::Zstd)
        } else {
            Err("unrecognized parquet file extension".into())
        }
    }
}
