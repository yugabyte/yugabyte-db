// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

//! The C++ `JsonWriter` (`yb/util/jsonwriter.cc`): rapidjson's `Writer` (`COMPACT`) or
//! `PrettyWriter` (`PRETTY`) writing as it goes, so a handler that returns early leaves an
//! unterminated document exactly as the C++ one does. YBA and scripts parse these bytes.

use protobuf::reflect::{ReflectValueRef, RuntimeFieldType};
use protobuf::MessageDyn;

/// `JsonWriter::Mode`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    /// rapidjson `Writer`: no whitespace.
    Compact,
    /// rapidjson `PrettyWriter`: four-space indent, `"key": value`.
    Pretty,
}

struct Level {
    in_array: bool,
    value_count: usize,
}

/// Writes one JSON document. Keys are written with [`JsonWriter::string`], as in C++.
pub struct JsonWriter {
    out: Vec<u8>,
    mode: Mode,
    stack: Vec<Level>,
}

impl JsonWriter {
    /// An empty document.
    pub fn new(mode: Mode) -> JsonWriter {
        JsonWriter {
            out: Vec::new(),
            mode,
            stack: Vec::new(),
        }
    }

    /// The bytes written so far.
    pub fn into_bytes(self) -> Vec<u8> {
        self.out
    }

    // rapidjson Writer::Prefix and PrettyWriter::PrettyPrefix.
    fn prefix(&mut self) {
        let pretty = self.mode == Mode::Pretty;
        let depth = self.stack.len();
        let Some(level) = self.stack.last_mut() else {
            return;
        };
        let count = level.value_count;
        level.value_count += 1;
        if level.in_array {
            if count > 0 {
                self.out.push(b',');
            }
            if pretty {
                self.out.push(b'\n');
                indent(&mut self.out, depth);
            }
        } else if count > 0 {
            if count % 2 == 0 {
                self.out.push(b',');
                if pretty {
                    self.out.push(b'\n');
                    indent(&mut self.out, depth);
                }
            } else {
                self.out.push(b':');
                if pretty {
                    self.out.push(b' ');
                }
            }
        } else if pretty {
            self.out.push(b'\n');
            indent(&mut self.out, depth);
        }
    }

    /// `StartObject`.
    pub fn start_object(&mut self) {
        self.prefix();
        self.stack.push(Level {
            in_array: false,
            value_count: 0,
        });
        self.out.push(b'{');
    }

    /// `EndObject`.
    pub fn end_object(&mut self) {
        self.end(b'}');
    }

    /// `StartArray`.
    pub fn start_array(&mut self) {
        self.prefix();
        self.stack.push(Level {
            in_array: true,
            value_count: 0,
        });
        self.out.push(b'[');
    }

    /// `EndArray`.
    pub fn end_array(&mut self) {
        self.end(b']');
    }

    fn end(&mut self, close: u8) {
        let level = self.stack.pop().expect("end without a start");
        if self.mode == Mode::Pretty && level.value_count > 0 {
            self.out.push(b'\n');
            indent(&mut self.out, self.stack.len());
        }
        self.out.push(close);
    }

    /// `String`, for keys and values. Bytes are written as they are, as rapidjson does without
    /// encoding validation.
    pub fn string(&mut self, s: impl AsRef<[u8]>) {
        self.prefix();
        escape_into(&mut self.out, s.as_ref());
    }

    /// A key and its string value.
    pub fn field(&mut self, key: &str, value: impl AsRef<[u8]>) {
        self.string(key);
        self.string(value);
    }

    /// `Null`.
    pub fn null(&mut self) {
        self.prefix();
        self.out.extend_from_slice(b"null");
    }

    /// `Bool`.
    pub fn bool(&mut self, b: bool) {
        self.prefix();
        self.out
            .extend_from_slice(if b { b"true" } else { b"false" });
    }

    /// `Int` and `Int64`.
    pub fn int(&mut self, i: i64) {
        self.prefix();
        self.out.extend_from_slice(i.to_string().as_bytes());
    }

    /// `Uint` and `Uint64`.
    pub fn uint(&mut self, u: u64) {
        self.prefix();
        self.out.extend_from_slice(u.to_string().as_bytes());
    }

    /// `Double`. rapidjson refuses NaN and infinities and writes nothing.
    pub fn double(&mut self, d: f64) {
        if !d.is_finite() {
            return;
        }
        self.prefix();
        self.out.extend_from_slice(format_double(d).as_bytes());
    }

    /// `JsonWriter::Protobuf`: the set fields by field number (`Reflection::ListFields`), enums by
    /// name, bytes as strings.
    pub fn protobuf(&mut self, message: &dyn MessageDyn) {
        let descriptor = message.descriptor_dyn();
        let mut fields: Vec<_> = descriptor
            .fields()
            .filter(|f| match f.runtime_field_type() {
                RuntimeFieldType::Singular(_) => f.has_field(message),
                RuntimeFieldType::Repeated(_) => !f.get_repeated(message).is_empty(),
                RuntimeFieldType::Map(..) => !f.get_map(message).is_empty(),
            })
            .collect();
        fields.sort_by_key(|f| f.number());
        self.start_object();
        for field in fields {
            self.string(field.name());
            match field.runtime_field_type() {
                RuntimeFieldType::Singular(_) => {
                    if let Some(value) = field.get_singular(message) {
                        self.reflect_value(value);
                    }
                }
                RuntimeFieldType::Repeated(_) => {
                    self.start_array();
                    let repeated = field.get_repeated(message);
                    for i in 0..repeated.len() {
                        self.reflect_value(repeated.get(i));
                    }
                    self.end_array();
                }
                RuntimeFieldType::Map(..) => {
                    // A map is a repeated entry message to C++.
                    self.start_array();
                    let map = field.get_map(message);
                    let mut entries: Vec<_> = map.into_iter().collect();
                    entries.sort_by(|a, b| {
                        format!("{:?}", a.0)
                            .partial_cmp(&format!("{:?}", b.0))
                            .unwrap_or(std::cmp::Ordering::Equal)
                    });
                    for (key, value) in entries {
                        self.start_object();
                        self.string("key");
                        self.reflect_value(key);
                        self.string("value");
                        self.reflect_value(value);
                        self.end_object();
                    }
                    self.end_array();
                }
            }
        }
        self.end_object();
    }

    fn reflect_value(&mut self, value: ReflectValueRef<'_>) {
        match value {
            ReflectValueRef::U32(v) => self.uint(v.into()),
            ReflectValueRef::U64(v) => self.uint(v),
            ReflectValueRef::I32(v) => self.int(v.into()),
            ReflectValueRef::I64(v) => self.int(v),
            ReflectValueRef::F32(v) => self.double(v.into()),
            ReflectValueRef::F64(v) => self.double(v),
            ReflectValueRef::Bool(v) => self.bool(v),
            ReflectValueRef::String(v) => self.string(v),
            ReflectValueRef::Bytes(v) => self.string(v),
            ReflectValueRef::Enum(descriptor, number) => match descriptor.value_by_number(number) {
                Some(v) => self.string(v.name()),
                // C++ keeps an unknown enum value in the unknown fields, so the field reads as
                // its default; this only happens for a value newer than this binary.
                None => self.int(number.into()),
            },
            ReflectValueRef::Message(m) => self.protobuf(&*m),
        }
    }
}

fn indent(out: &mut Vec<u8>, depth: usize) {
    out.resize(out.len() + 4 * depth, b' ');
}

// rapidjson Writer::WriteString without kWriteValidateEncodingFlag.
fn escape_into(out: &mut Vec<u8>, s: &[u8]) {
    const HEX: &[u8; 16] = b"0123456789ABCDEF";
    out.push(b'"');
    for &c in s {
        match c {
            b'"' => out.extend_from_slice(b"\\\""),
            b'\\' => out.extend_from_slice(b"\\\\"),
            0x08 => out.extend_from_slice(b"\\b"),
            0x0C => out.extend_from_slice(b"\\f"),
            b'\n' => out.extend_from_slice(b"\\n"),
            b'\r' => out.extend_from_slice(b"\\r"),
            b'\t' => out.extend_from_slice(b"\\t"),
            c if c < 0x20 => {
                out.extend_from_slice(b"\\u00");
                out.push(HEX[usize::from(c >> 4)]);
                out.push(HEX[usize::from(c & 0xF)]);
            }
            c => out.push(c),
        }
    }
    out.push(b'"');
}

/// rapidjson's `dtoa`: the shortest digits, laid out by `Prettify`. "0.0", "1.5", "100.0",
/// "0.001234", "1e30", "1.234e-7".
pub fn format_double(d: f64) -> String {
    if d == 0.0 {
        return if d.is_sign_negative() { "-0.0" } else { "0.0" }.to_string();
    }
    let mut out = String::new();
    if d < 0.0 {
        out.push('-');
    }
    // `{:e}` is the shortest round-trip representation: "1.2345e-5".
    let scientific = format!("{:e}", d.abs());
    let (mantissa, exponent) = scientific.split_once('e').expect("{:e} has an exponent");
    let exponent: i32 = exponent.parse().expect("{:e} exponent is an integer");
    let digits: String = mantissa.chars().filter(|c| *c != '.').collect();
    let length = digits.len() as i32;
    // digits * 10^k == value.
    let k = exponent - (length - 1);
    let kk = length + k;
    if (0..=21).contains(&kk) && k >= 0 {
        out.push_str(&digits);
        out.extend(std::iter::repeat_n('0', k as usize));
        out.push_str(".0");
    } else if 0 < kk && kk <= 21 {
        out.push_str(&digits[..kk as usize]);
        out.push('.');
        out.push_str(&digits[kk as usize..]);
    } else if -6 < kk && kk <= 0 {
        out.push_str("0.");
        out.extend(std::iter::repeat_n('0', (-kk) as usize));
        out.push_str(&digits);
    } else if length == 1 {
        out.push_str(&digits);
        out.push('e');
        out.push_str(&(kk - 1).to_string());
    } else {
        out.push_str(&digits[..1]);
        out.push('.');
        out.push_str(&digits[1..]);
        out.push('e');
        out.push_str(&(kk - 1).to_string());
    }
    out
}

/// `HumanizeBytes` (`gutil/strings/numbers.cc`): decimal units, two decimals.
pub fn humanize_bytes(bytes: u64) -> String {
    const UNITS: [&str; 7] = ["B", "KB", "MB", "GB", "TB", "PB", "EB"];
    if bytes < 1000 {
        return format!("{bytes} B");
    }
    let mut value = bytes as f64;
    let mut unit = 0;
    while value >= 1000.0 && unit + 1 < UNITS.len() {
        value /= 1000.0;
        unit += 1;
    }
    let scale = 100.0;
    value = (value * scale).round() / scale;
    while value >= 1000.0 && unit + 1 < UNITS.len() {
        value /= 1000.0;
        value = (value * scale).round() / scale;
        unit += 1;
    }
    format!("{value:.2} {}", UNITS[unit])
}

/// The name of an enum value, as `<Enum>_Name` gives it.
pub fn enum_name<E: protobuf::EnumFull>(value: E) -> String {
    value.descriptor().name().to_string()
}

/// `FixedPoint(value, precision)`: `printf("%.*f")`.
pub fn fixed_point(value: f64, precision: usize) -> String {
    format!("{value:.precision$}")
}
