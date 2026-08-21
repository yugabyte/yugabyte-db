#include "duckdb.hpp"
#include "duckdb/common/shared_ptr.hpp"
#include "duckdb/common/extra_type_info.hpp"
#include "duckdb/common/types/bit.hpp"
#include "duckdb/common/types/blob.hpp"
#include "duckdb/common/types/uuid.hpp"

#include "pgduckdb/pgduckdb_guc.hpp"
#include "pgduckdb/pgduckdb_types.hpp"
#include "pgduckdb/pgduckdb_metadata_cache.hpp"
#include "pgduckdb/pgduckdb_utils.hpp"
#include "pgduckdb/pgduckdb_metadata_cache.hpp"
#include "pgduckdb/scan/postgres_scan.hpp"
#include "pgduckdb/pg/memory.hpp"
#include "pgduckdb/pg/types.hpp"

extern "C" {

#include "pgduckdb/vendor/pg_numeric_c.hpp"

#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "access/tupdesc_details.h"
#include "catalog/pg_type.h"
#include "common/int.h"
#include "executor/tuptable.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/date.h"
#include "utils/jsonb.h"
#include "utils/lsyscache.h"
#include "utils/numeric.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"
#include "utils/uuid.h"
#include "utils/varbit.h"
}

#include "pgduckdb/pgduckdb_detoast.hpp"
#include "pgduckdb/pgduckdb_process_lock.hpp"

namespace pgduckdb {

NumericVar FromNumeric(Numeric num);

struct NumericAsDouble : public duckdb::ExtraTypeInfo {
	// Dummy struct to indicate at conversion that the source is a Numeric
public:
	NumericAsDouble() : ExtraTypeInfo(duckdb::ExtraTypeInfoType::INVALID_TYPE_INFO) {
	}

	duckdb::shared_ptr<ExtraTypeInfo>
	Copy() const override {
		return duckdb::make_shared_ptr<NumericAsDouble>(*this);
	}
};

// FIXME: perhaps we want to just make a generic ExtraTypeInfo that holds the Postgres type OID
struct IsBpChar : public duckdb::ExtraTypeInfo {
public:
	IsBpChar() : ExtraTypeInfo(duckdb::ExtraTypeInfoType::INVALID_TYPE_INFO) {
	}

	duckdb::shared_ptr<ExtraTypeInfo>
	Copy() const override {
		return duckdb::make_shared_ptr<IsBpChar>(*this);
	}
};

using duckdb::hugeint_t;
using duckdb::uhugeint_t;

struct DecimalConversionInteger {
	static int64_t
	GetPowerOfTen(idx_t index) {
		static const int64_t POWERS_OF_TEN[] {1,
		                                      10,
		                                      100,
		                                      1000,
		                                      10000,
		                                      100000,
		                                      1000000,
		                                      10000000,
		                                      100000000,
		                                      1000000000,
		                                      10000000000,
		                                      100000000000,
		                                      1000000000000,
		                                      10000000000000,
		                                      100000000000000,
		                                      1000000000000000,
		                                      10000000000000000,
		                                      100000000000000000,
		                                      1000000000000000000};
		if (index >= 19) {
			throw duckdb::InternalException("DecimalConversionInteger::GetPowerOfTen - Out of range");
		}
		return POWERS_OF_TEN[index];
	}

	template <class T>
	static T
	Finalize(const NumericVar &, T result) {
		return result;
	}
};

struct DecimalConversionHugeint {
	static hugeint_t
	GetPowerOfTen(idx_t index) {
		static const hugeint_t POWERS_OF_TEN[] {
		    hugeint_t(1),
		    hugeint_t(10),
		    hugeint_t(100),
		    hugeint_t(1000),
		    hugeint_t(10000),
		    hugeint_t(100000),
		    hugeint_t(1000000),
		    hugeint_t(10000000),
		    hugeint_t(100000000),
		    hugeint_t(1000000000),
		    hugeint_t(10000000000),
		    hugeint_t(100000000000),
		    hugeint_t(1000000000000),
		    hugeint_t(10000000000000),
		    hugeint_t(100000000000000),
		    hugeint_t(1000000000000000),
		    hugeint_t(10000000000000000),
		    hugeint_t(100000000000000000),
		    hugeint_t(1000000000000000000),
		    hugeint_t(1000000000000000000) * hugeint_t(10),
		    hugeint_t(1000000000000000000) * hugeint_t(100),
		    hugeint_t(1000000000000000000) * hugeint_t(1000),
		    hugeint_t(1000000000000000000) * hugeint_t(10000),
		    hugeint_t(1000000000000000000) * hugeint_t(100000),
		    hugeint_t(1000000000000000000) * hugeint_t(1000000),
		    hugeint_t(1000000000000000000) * hugeint_t(10000000),
		    hugeint_t(1000000000000000000) * hugeint_t(100000000),
		    hugeint_t(1000000000000000000) * hugeint_t(1000000000),
		    hugeint_t(1000000000000000000) * hugeint_t(10000000000),
		    hugeint_t(1000000000000000000) * hugeint_t(100000000000),
		    hugeint_t(1000000000000000000) * hugeint_t(1000000000000),
		    hugeint_t(1000000000000000000) * hugeint_t(10000000000000),
		    hugeint_t(1000000000000000000) * hugeint_t(100000000000000),
		    hugeint_t(1000000000000000000) * hugeint_t(1000000000000000),
		    hugeint_t(1000000000000000000) * hugeint_t(10000000000000000),
		    hugeint_t(1000000000000000000) * hugeint_t(100000000000000000),
		    hugeint_t(1000000000000000000) * hugeint_t(1000000000000000000),
		    hugeint_t(1000000000000000000) * hugeint_t(1000000000000000000) * hugeint_t(10),
		    hugeint_t(1000000000000000000) * hugeint_t(1000000000000000000) * hugeint_t(100)};
		if (index >= 39) {
			throw duckdb::InternalException("DecimalConversionHugeint::GetPowerOfTen - Out of range");
		}
		return POWERS_OF_TEN[index];
	}

	static hugeint_t
	Finalize(const NumericVar &, hugeint_t result) {
		return result;
	}
};

struct DecimalConversionDouble {
	static double
	GetPowerOfTen(idx_t index) {
		return pow(10, double(index));
	}

	static double
	Finalize(const NumericVar &numeric, double result) {
		return result / GetPowerOfTen(numeric.dscale);
	}
};

// Util function to convert duckdb `BIT` value to postgres bitstring type.
// There're two possible corresponding types `BITOID` and `VARBITOID`, here we convert to `VARBITOID` for generality.
static Datum
ConvertVarbitDatum(const duckdb::Value &value) {
	const std::string value_str = value.ToString();

	// Here we rely on postgres conversion function, instead of manual parsing, because BIT string type involves padding
	// and duckdb/postgres handle it differently, it's non-trivial to memcpy the bits.
	return pgduckdb::pg::StringToVarbit(value_str.c_str());
}

static inline bool
ValidDate(duckdb::date_t dt) {
	if (dt == duckdb::date_t::infinity() || dt == duckdb::date_t::ninfinity())
		return true;
	return dt >= pgduckdb::PGDUCKDB_PG_MIN_DATE_VALUE && dt <= pgduckdb::PGDUCKDB_PG_MAX_DATE_VALUE;
}

static inline bool
ValidTimestampOrTimestampTz(int64_t timestamp) {
	// PG TIMESTAMP RANGE = 4714-11-24 00:00:00 (BC) <-> 294276-12-31 23:59:59
	// DUCK TIMESTAMP RANGE = 290308-12-22 00:00:00 (BC) <-> 294247-01-10 04:00:54
	// Taking Intersection of the ranges
	// MIN TIMESTAMP = 4714-11-24 00:00:00 (BC)
	// MAX TIMESTAMP = 294246-12-31 23:59:59 , To keep it capped to a specific year.. also coincidently this is EXACTLY
	// 30 years less than PG max value.
	return timestamp >= pgduckdb::PGDUCKDB_MIN_TIMESTAMP_VALUE && timestamp < pgduckdb::PGDUCKDB_MAX_TIMESTAMP_VALUE;
}

static Datum
ConvertToStringDatum(const duckdb::Value &value) {
	auto str = value.ToString();
	auto varchar = str.c_str();
	auto varchar_len = str.size();

	text *result = (text *)palloc0(varchar_len + VARHDRSZ);
	SET_VARSIZE(result, varchar_len + VARHDRSZ);
	memcpy(VARDATA(result), varchar, varchar_len);
	return PointerGetDatum(result);
}

static inline Datum
ConvertBoolDatum(const duckdb::Value &value) {
	return value.GetValue<bool>();
}

static inline Datum
ConvertCharDatum(const duckdb::Value &value) {
	return value.GetValue<int8_t>();
}

static inline Datum
ConvertInt2Datum(const duckdb::Value &value) {
	if (value.type().id() == duckdb::LogicalTypeId::UTINYINT) {
		return UInt8GetDatum(value.GetValue<uint8_t>());
	}
	return Int16GetDatum(value.GetValue<int16_t>());
}

static inline Datum
ConvertInt4Datum(const duckdb::Value &value) {
	if (value.type().id() == duckdb::LogicalTypeId::USMALLINT) {
		return UInt16GetDatum(value.GetValue<uint16_t>());
	}
	return Int32GetDatum(value.GetValue<int32_t>());
}

static inline Datum
ConvertInt8Datum(const duckdb::Value &value) {
	if (value.type().id() == duckdb::LogicalTypeId::UINTEGER) {
		return UInt32GetDatum(value.GetValue<uint32_t>());
	}
	return Int64GetDatum(value.GetValue<int64_t>());
}

static Datum
ConvertBinaryDatum(const duckdb::Value &value) {
	auto str = value.GetValueUnsafe<duckdb::string_t>();
	auto blob_len = str.GetSize();
	auto blob = str.GetDataUnsafe();
	bytea *result = (bytea *)palloc0(blob_len + VARHDRSZ);
	SET_VARSIZE(result, blob_len + VARHDRSZ);
	memcpy(VARDATA(result), blob, blob_len);
	return PointerGetDatum(result);
}

inline Datum
ConvertDateDatum(const duckdb::Value &value) {
	duckdb::date_t date = value.GetValue<duckdb::date_t>();
	if (!ValidDate(date))
		throw duckdb::OutOfRangeException("The value should be between min and max value (%s <-> %s)",
		                                  duckdb::Date::ToString(pgduckdb::PGDUCKDB_PG_MIN_DATE_VALUE),
		                                  duckdb::Date::ToString(pgduckdb::PGDUCKDB_PG_MAX_DATE_VALUE));

	// Special Handling for +/-infinity date values
	// -infinity value is different for PG date
	if (date == duckdb::date_t::ninfinity())
		return DateADTGetDatum(DATEVAL_NOBEGIN);
	else if (date == duckdb::date_t::infinity())
		return DateADTGetDatum(DATEVAL_NOEND);

	return DateADTGetDatum(date.days - pgduckdb::PGDUCKDB_DUCK_DATE_OFFSET);
}

static Datum
ConvertIntervalDatum(const duckdb::Value &value) {
	duckdb::interval_t duckdb_interval = value.GetValue<duckdb::interval_t>();
	Interval *pg_interval = static_cast<Interval *>(palloc(sizeof(Interval)));
	pg_interval->month = duckdb_interval.months;
	pg_interval->day = duckdb_interval.days;
	pg_interval->time = duckdb_interval.micros;
	return IntervalPGetDatum(pg_interval);
}

static Datum
ConvertTimeDatum(const duckdb::Value &value) {
	const int64_t microsec = value.GetValue<int64_t>();
	const TimeADT pg_time = microsec;
	return Int64GetDatum(pg_time);
}

static Datum
ConvertTimeTzDatum(const duckdb::Value &value) {
	duckdb::dtime_tz_t dt_tz = value.GetValue<duckdb::dtime_tz_t>();
	const int64_t micros = dt_tz.time().micros;
	const int32_t tz_offset = dt_tz.offset();

	TimeTzADT *result = static_cast<TimeTzADT *>(palloc(sizeof(TimeTzADT)));
	result->time = micros;
	// pg and duckdb stores timezone with different signs, for example, for TIMETZ 01:02:03+05, duckdb stores offset =
	// 18000, while pg stores zone = -18000.
	result->zone = -tz_offset;
	return TimeTzADTPGetDatum(result);
}

inline Datum
ConvertTimestampDatum(const duckdb::Value &value) {
	// Extract raw int64_t value of timestamp
	int64_t rawValue = value.GetValue<int64_t>();

	// Early Return for +/-Inf
	if (rawValue == static_cast<int64_t>(duckdb::timestamp_t::ninfinity()))
		return TimestampGetDatum(DT_NOBEGIN);
	else if (rawValue == static_cast<int64_t>(duckdb::timestamp_t::infinity()))
		return TimestampGetDatum(DT_NOEND);

	// Handle specific Timestamp unit(sec, ms, ns) types
	switch (value.type().id()) {
	case duckdb::LogicalType::TIMESTAMP_MS:
		// 1 ms = 10^3 micro-sec
		rawValue *= 1000;
		break;
	case duckdb::LogicalType::TIMESTAMP_NS:
		// 1 ns = 10^-3 micro-sec
		rawValue /= 1000;
		break;
	case duckdb::LogicalType::TIMESTAMP_S:
		// 1 s = 10^6 micro-sec
		rawValue *= 1000000;
		break;
	default:
		// Since we don't want to handle anything here
		break;
	}

	if (!ValidTimestampOrTimestampTz(rawValue))
		throw duckdb::OutOfRangeException(
		    "The Timestamp value should be between min and max value (%s <-> %s)",
		    duckdb::Timestamp::ToString(static_cast<duckdb::timestamp_t>(PGDUCKDB_MIN_TIMESTAMP_VALUE)),
		    duckdb::Timestamp::ToString(static_cast<duckdb::timestamp_t>(PGDUCKDB_MAX_TIMESTAMP_VALUE)));

	return TimestampGetDatum(rawValue - pgduckdb::PGDUCKDB_DUCK_TIMESTAMP_OFFSET);
}

inline Datum
ConvertTimestampTzDatum(const duckdb::Value &value) {
	duckdb::timestamp_tz_t timestamp = value.GetValue<duckdb::timestamp_tz_t>();
	int64_t rawValue = timestamp.value;

	// Early Return for +/-Inf
	if (rawValue == static_cast<int64_t>(duckdb::timestamp_t::ninfinity()))
		return TimestampTzGetDatum(DT_NOBEGIN);
	else if (rawValue == static_cast<int64_t>(duckdb::timestamp_t::infinity()))
		return TimestampTzGetDatum(DT_NOEND);

	if (!ValidTimestampOrTimestampTz(rawValue))
		throw duckdb::OutOfRangeException(
		    "The TimestampTz value should be between min and max value (%s <-> %s)",
		    duckdb::Timestamp::ToString(static_cast<duckdb::timestamp_tz_t>(PGDUCKDB_MIN_TIMESTAMP_VALUE)),
		    duckdb::Timestamp::ToString(static_cast<duckdb::timestamp_tz_t>(PGDUCKDB_MAX_TIMESTAMP_VALUE)));

	return TimestampTzGetDatum(rawValue - pgduckdb::PGDUCKDB_DUCK_TIMESTAMP_OFFSET);
}

inline Datum
ConvertFloatDatum(const duckdb::Value &value) {
	return Float4GetDatum(value.GetValue<float>());
}

inline Datum
ConvertDoubleDatum(const duckdb::Value &value) {
	return Float8GetDatum(value.GetValue<double>());
}

template <class T, class OP = DecimalConversionInteger>
void
ConvertNumeric(const duckdb::Value &ddb_value, idx_t scale, NumericVar &result) {
	result.dscale = scale;

	T value = ddb_value.GetValueUnsafe<T>();
	if (value < 0) {
		value = -value;
		result.sign = NUMERIC_NEG;
	} else {
		result.sign = NUMERIC_POS;
	}

	// divide the decimal into the integer part (before the decimal point) and fractional part (after the point)
	T integer_part;
	T fractional_part;
	if (scale == 0) {
		integer_part = value;
		fractional_part = 0;
	} else {
		integer_part = value / T(OP::GetPowerOfTen(scale));
		fractional_part = value % T(OP::GetPowerOfTen(scale));
	}

	constexpr idx_t MAX_DIGITS = sizeof(T) * 4;
	uint16_t integral_digits[MAX_DIGITS];
	uint16_t fractional_digits[MAX_DIGITS];
	int32_t integral_ndigits;

	// split the integral part into parts of up to NBASE (4 digits => 0..9999)
	integral_ndigits = 0;
	while (integer_part > 0) {
		integral_digits[integral_ndigits++] = uint16_t(integer_part % T(NBASE));
		integer_part /= T(NBASE);
	}

	result.weight = integral_ndigits - 1;
	// split the fractional part into parts of up to NBASE (4 digits => 0..9999)
	// count the amount of digits required for the fractional part
	// note that while it is technically possible to leave out zeros here this adds even more complications
	// so we just always write digits for the full "scale", even if not strictly required
	idx_t fractional_ndigits = (scale + DEC_DIGITS - 1) / DEC_DIGITS;
	// fractional digits are LEFT aligned (for some unknown reason)
	// that means if we write ".12" with a scale of 2 we actually need to write "1200", instead of "12"
	// this means we need to "correct" the number 12 by multiplying by 100 in this case
	// this correction factor is the "number of digits to the next full number"
	int32_t correction = fractional_ndigits * DEC_DIGITS - scale;
	fractional_part *= T(OP::GetPowerOfTen(correction));
	for (idx_t i = 0; i < fractional_ndigits; i++) {
		fractional_digits[i] = uint16_t(fractional_part % NBASE);
		fractional_part /= NBASE;
	}

	result.ndigits = integral_ndigits + fractional_ndigits;

	result.buf = (NumericDigit *)palloc(result.ndigits * sizeof(NumericDigit));
	result.digits = result.buf;
	auto &digits = result.digits;

	idx_t digits_idx = 0;
	for (idx_t i = integral_ndigits; i > 0; i--) {
		digits[digits_idx++] = integral_digits[i - 1];
	}
	for (idx_t i = fractional_ndigits; i > 0; i--) {
		digits[digits_idx++] = fractional_digits[i - 1];
	}
}

static Datum
ConvertNumericDatum(const duckdb::Value &value) {
	auto value_type_id = value.type().id();

	// Special handle duckdb BIGNUM type.
	if (value.type().id() == duckdb::LogicalTypeId::BIGNUM) {
		// The performant way to handle the translation is to parse BIGNUM out, here we leverage string conversion and
		// parsing mainly for code simplicity.
		const std::string value_str = value.ToString();
		Datum pg_numeric = pgduckdb::pg::StringToNumeric(value_str.c_str());
		return pg_numeric;
	}

	// Special handle duckdb DOUBLE TYPE.
	if (value_type_id == duckdb::LogicalTypeId::DOUBLE) {
		return ConvertDoubleDatum(value);
	}

	NumericVar numeric_var;
	D_ASSERT(value_type_id == duckdb::LogicalTypeId::DECIMAL || value_type_id == duckdb::LogicalTypeId::HUGEINT ||
	         value_type_id == duckdb::LogicalTypeId::UBIGINT || value_type_id == duckdb::LogicalTypeId::UHUGEINT);
	const bool is_decimal = value_type_id == duckdb::LogicalTypeId::DECIMAL;
	uint8_t scale = is_decimal ? duckdb::DecimalType::GetScale(value.type()) : 0;

	switch (value.type().InternalType()) {
	case duckdb::PhysicalType::INT16:
		ConvertNumeric<int16_t>(value, scale, numeric_var);
		break;
	case duckdb::PhysicalType::INT32:
		ConvertNumeric<int32_t>(value, scale, numeric_var);
		break;
	case duckdb::PhysicalType::INT64:
		ConvertNumeric<int64_t>(value, scale, numeric_var);
		break;
	case duckdb::PhysicalType::UINT64:
		ConvertNumeric<uint64_t>(value, scale, numeric_var);
		break;
	case duckdb::PhysicalType::INT128:
		ConvertNumeric<hugeint_t, DecimalConversionHugeint>(value, scale, numeric_var);
		break;
	case duckdb::PhysicalType::UINT128:
		ConvertNumeric<uhugeint_t, DecimalConversionHugeint>(value, scale, numeric_var);
		break;
	default:
		throw duckdb::InvalidInputException(
		    "(PGDuckDB/ConvertNumericDatum) Unrecognized physical type for DECIMAL value");
	}

	auto numeric = PostgresFunctionGuard(make_result, &numeric_var);
	return NumericGetDatum(numeric);
}

static Datum
ConvertUUIDDatum(const duckdb::Value &value) {
	D_ASSERT(value.type().id() == duckdb::LogicalTypeId::UUID);
	D_ASSERT(value.type().InternalType() == duckdb::PhysicalType::INT128);
	auto duckdb_uuid = value.GetValue<hugeint_t>();
	pg_uuid_t *postgres_uuid = (pg_uuid_t *)palloc(sizeof(pg_uuid_t));

	duckdb_uuid.upper ^= (uint64_t(1) << 63);
	// Convert duckdb_uuid to bytes and store in postgres_uuid.data
	uint8_t *uuid_bytes = (uint8_t *)&duckdb_uuid;

	for (int i = 0; i < UUID_LEN; ++i) {
		postgres_uuid->data[i] = uuid_bytes[UUID_LEN - 1 - i];
	}

	return UUIDPGetDatum(postgres_uuid);
}

inline Datum
ConvertDuckStructDatum(const duckdb::Value &value) {
	D_ASSERT(value.type().id() == duckdb::LogicalTypeId::STRUCT);
	return ConvertToStringDatum(value);
}

static Datum
ConvertUnionDatum(const duckdb::Value &value) {
	D_ASSERT(value.type().id() == duckdb::LogicalTypeId::UNION);
	return ConvertToStringDatum(value);
}

static Datum
ConvertMapDatum(const duckdb::Value &value) {
	D_ASSERT(value.type().id() == duckdb::LogicalTypeId::MAP);
	return ConvertToStringDatum(value);
}

static duckdb::interval_t
DatumGetInterval(Datum value) {
	Interval *pg_interval = DatumGetIntervalP(value);
	duckdb::interval_t duck_interval;
	duck_interval.months = pg_interval->month;
	duck_interval.days = pg_interval->day;
	duck_interval.micros = pg_interval->time;
	return duck_interval;
}

static std::string
DatumGetBitString(Datum value) {
	// Here we rely on postgres conversion function, instead of manual parsing,
	// because BIT string type involves padding and duckdb/postgres handle it
	// differently, it's non-trivial to memcpy the bits.
	//
	// NOTE: We use VarbitToString here, because BIT and VARBIT are both stored
	// internally as a VARBIT in postgres.
	return std::string(pgduckdb::pg::VarbitToString(value));
}

static duckdb::dtime_t
DatumGetTime(Datum value) {
	const TimeADT pg_time = DatumGetTimeADT(value);
	duckdb::dtime_t duckdb_time {pg_time};
	return duckdb_time;
}

static duckdb::dtime_tz_t
DatumGetTimeTz(Datum value) {
	TimeTzADT *tzt = static_cast<TimeTzADT *>(DatumGetTimeTzADTP(value));
	// pg and duckdb stores timezone with different signs, for example, for TIMETZ 01:02:03+05, duckdb stores offset =
	// 18000, while pg stores zone = -18000.
	const uint64_t bits = duckdb::dtime_tz_t::encode_micros(static_cast<int64_t>(tzt->time)) |
	                      duckdb::dtime_tz_t::encode_offset(-tzt->zone);
	const duckdb::dtime_tz_t duck_time_tz {bits};
	return duck_time_tz;
}

static hugeint_t
DatumGetUUID(Datum value) {
	const Pointer pg_uuid = DatumGetPointer(value);
	hugeint_t duck_uuid;
	D_ASSERT(UUID_LEN == sizeof(hugeint_t));
	for (idx_t i = 0; i < UUID_LEN; i++) {
		((uint8_t *)&duck_uuid)[UUID_LEN - 1 - i] = ((uint8_t *)pg_uuid)[i];
	}
	duck_uuid.upper ^= (uint64_t(1) << 63);
	return duck_uuid;
}

template <int32_t OID>
struct PostgresTypeTraits;

// Specializations for each type
// BOOL type
template <>
struct PostgresTypeTraits<BOOLOID> {
	static constexpr int16_t typlen = 1;
	static constexpr bool typbyval = true;
	static constexpr char typalign = 'c';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertBoolDatum(val);
	}
};

// CHAR type
template <>
struct PostgresTypeTraits<CHAROID> {
	static constexpr int16_t typlen = 1;
	static constexpr bool typbyval = true;
	static constexpr char typalign = 'c';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertCharDatum(val);
	}
};

// INT2 type (smallint)
template <>
struct PostgresTypeTraits<INT2OID> {
	static constexpr int16_t typlen = 2;
	static constexpr bool typbyval = true;
	static constexpr char typalign = 's';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertInt2Datum(val);
	}
};

// INT4 type (integer)
template <>
struct PostgresTypeTraits<INT4OID> {
	static constexpr int16_t typlen = 4;
	static constexpr bool typbyval = true;
	static constexpr char typalign = 'i';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertInt4Datum(val);
	}
};

// INT8 type (bigint)
template <>
struct PostgresTypeTraits<INT8OID> {
	static constexpr int16_t typlen = 8;
	static constexpr bool typbyval = true;
	static constexpr char typalign = 'd';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertInt8Datum(val);
	}
};

// FLOAT4 type (real)
template <>
struct PostgresTypeTraits<FLOAT4OID> {
	static constexpr int16_t typlen = 4;
	static constexpr bool typbyval = true;
	static constexpr char typalign = 'i';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertFloatDatum(val);
	}
};

// FLOAT8 type (double precision)
template <>
struct PostgresTypeTraits<FLOAT8OID> {
	static constexpr int16_t typlen = 8;
	static constexpr bool typbyval = true;
	static constexpr char typalign = 'd';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertDoubleDatum(val);
	}
};

// TIMESTAMP type
template <>
struct PostgresTypeTraits<TIMESTAMPOID> {
	static constexpr int16_t typlen = 8;
	static constexpr bool typbyval = true;
	static constexpr char typalign = 'd';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertTimestampDatum(val);
	}
};

template <>
struct PostgresTypeTraits<TIMESTAMPTZOID> {
	static constexpr int16_t typlen = 8;
	static constexpr bool typbyval = true;
	static constexpr char typalign = 'd';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertTimestampTzDatum(val);
	}
};

// INTERVAL type
template <>
struct PostgresTypeTraits<INTERVALOID> {
	static constexpr int16_t typlen = 16;
	static constexpr bool typbyval = false;
	static constexpr char typalign = 'c';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertIntervalDatum(val);
	}
};

// BIT type
template <>
struct PostgresTypeTraits<VARBITOID> {
	static constexpr int16_t typlen = -1;
	static constexpr bool typbyval = false;
	static constexpr char typalign = 'i';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertVarbitDatum(val);
	}
};

// TIME type
template <>
struct PostgresTypeTraits<TIMEOID> {
	static constexpr int16_t typlen = 8;
	static constexpr bool typbyval = true;
	static constexpr char typalign = 'd';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertTimeDatum(val);
	}
};

// TIMETZ type
template <>
struct PostgresTypeTraits<TIMETZOID> {
	static constexpr int16_t typlen = 12;
	static constexpr bool typbyval = false;
	static constexpr char typalign = 'd';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertTimeTzDatum(val);
	}
};

// DATE type
template <>
struct PostgresTypeTraits<DATEOID> {
	static constexpr int16_t typlen = 4;
	static constexpr bool typbyval = true;
	static constexpr char typalign = 'i';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertDateDatum(val);
	}
};

// UUID type
template <>
struct PostgresTypeTraits<UUIDOID> {
	static constexpr int16_t typlen = 16;
	static constexpr bool typbyval = false;
	static constexpr char typalign = 'c';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertUUIDDatum(val);
	}
};

// NUMERIC type
template <>
struct PostgresTypeTraits<NUMERICOID> {
	static constexpr int16_t typlen = -1; // variable-length
	static constexpr bool typbyval = false;
	static constexpr char typalign = 'i';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertNumericDatum(val);
	}
};

// TEXT type
template <>
struct PostgresTypeTraits<TEXTOID> {
	static constexpr int16_t typlen = -1; // variable-length
	static constexpr bool typbyval = false;
	static constexpr char typalign = 'i';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertToStringDatum(val);
	}
};

// BLOB type
template <>
struct PostgresTypeTraits<BYTEAOID> {
	static constexpr int16_t typlen = -1; // variable-length
	static constexpr bool typbyval = false;
	static constexpr char typalign = 'i';

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return ConvertBinaryDatum(val);
	}
};

template <int32_t OID>
struct PostgresOIDMapping {
	static constexpr int32_t postgres_oid = OID;
	static constexpr int16_t typlen = PostgresTypeTraits<OID>::typlen;
	static constexpr bool typbyval = PostgresTypeTraits<OID>::typbyval;
	static constexpr char typalign = PostgresTypeTraits<OID>::typalign;

	static inline Datum
	ToDatum(const duckdb::Value &val) {
		return PostgresTypeTraits<OID>::ToDatum(val);
	}
};

template <class MAPPING>
struct PODArray {
public:
	static ArrayType *
	ConstructArray(Datum *datums, bool *nulls, int ndims, int *dims, int *lower_bound) {
		return construct_md_array(datums, nulls, ndims, dims, lower_bound, MAPPING::postgres_oid, MAPPING::typlen,
		                          MAPPING::typbyval, MAPPING::typalign);
	}

	static Datum
	ConvertToPostgres(const duckdb::Value &val) {
		return MAPPING::ToDatum(val);
	}
};

using BoolArray = PODArray<PostgresOIDMapping<BOOLOID>>;
using CharArray = PODArray<PostgresOIDMapping<CHAROID>>;
using Int2Array = PODArray<PostgresOIDMapping<INT2OID>>;
using Int4Array = PODArray<PostgresOIDMapping<INT4OID>>;
using Int8Array = PODArray<PostgresOIDMapping<INT8OID>>;
using Float4Array = PODArray<PostgresOIDMapping<FLOAT4OID>>;
using Float8Array = PODArray<PostgresOIDMapping<FLOAT8OID>>;
using DateArray = PODArray<PostgresOIDMapping<DATEOID>>;
using TimestampArray = PODArray<PostgresOIDMapping<TIMESTAMPOID>>;
using TimestampTzArray = PODArray<PostgresOIDMapping<TIMESTAMPTZOID>>;
using IntervalArray = PODArray<PostgresOIDMapping<INTERVALOID>>;
using BitArray = PODArray<PostgresOIDMapping<VARBITOID>>;
using TimeArray = PODArray<PostgresOIDMapping<TIMEOID>>;
using TimeTzArray = PODArray<PostgresOIDMapping<TIMETZOID>>;
using UUIDArray = PODArray<PostgresOIDMapping<UUIDOID>>;
using TextArray = PODArray<PostgresOIDMapping<TEXTOID>>;
using NumericArray = PODArray<PostgresOIDMapping<NUMERICOID>>;
using ByteArray = PODArray<PostgresOIDMapping<BYTEAOID>>;

// Complex type arrays with runtime OID determination
struct StructArray {
public:
	static ArrayType *
	ConstructArray(Datum *datums, bool *nulls, int ndims, int *dims, int *lower_bound) {
		return construct_md_array(datums, nulls, ndims, dims, lower_bound, pgduckdb::DuckdbStructOid(), -1, false, 'i');
	}

	static Datum
	ConvertToPostgres(const duckdb::Value &val) {
		return ConvertDuckStructDatum(val);
	}
};

struct UnionArray {
public:
	static ArrayType *
	ConstructArray(Datum *datums, bool *nulls, int ndims, int *dims, int *lower_bound) {
		return construct_md_array(datums, nulls, ndims, dims, lower_bound, pgduckdb::DuckdbUnionOid(), -1, false, 'i');
	}

	static Datum
	ConvertToPostgres(const duckdb::Value &val) {
		return ConvertUnionDatum(val);
	}
};

struct MapArray {
public:
	static ArrayType *
	ConstructArray(Datum *datums, bool *nulls, int ndims, int *dims, int *lower_bound) {
		return construct_md_array(datums, nulls, ndims, dims, lower_bound, pgduckdb::DuckdbMapOid(), -1, false, 'i');
	}

	static Datum
	ConvertToPostgres(const duckdb::Value &val) {
		return ConvertMapDatum(val);
	}
};

static bool
IsNestedType(const duckdb::LogicalTypeId type_id) {
	/* TODO: Add more nested type*/
	return type_id == duckdb::LogicalTypeId::LIST || type_id == duckdb::LogicalTypeId::ARRAY;
}

static const duckdb::LogicalType &
GetChildType(const duckdb::LogicalType &type) {
	/* TODO: Add more nested type*/
	switch (type.id()) {
	case duckdb::LogicalTypeId::LIST:
		return duckdb::ListType::GetChildType(type);
	case duckdb::LogicalTypeId::ARRAY:
		return duckdb::ArrayType::GetChildType(type);
	default:
		throw duckdb::InvalidInputException("Expected a LIST or ARRAY type, got '%s' instead", type.ToString());
	}
}

static idx_t
GetDuckDBListDimensionality(const duckdb::LogicalType &nested_type, idx_t depth = 0) {
	D_ASSERT(IsNestedType(nested_type.id()));
	auto &child = pgduckdb::GetChildType(nested_type);
	if (IsNestedType(child.id())) {
		return GetDuckDBListDimensionality(child, depth + 1);
	}
	return depth + 1;
}

namespace {

static duckdb::LogicalType
CreateUnsupportedPostgresType(std::string error_message) {
	duckdb::LogicalType type = duckdb::LogicalType::INVALID;
	type.SetAlias("UnsupportedPostgresType");
	auto info = duckdb::make_uniq<duckdb::ExtensionTypeInfo>();
	info->modifiers.emplace_back(duckdb::Value(error_message));
	type.SetExtensionInfo(std::move(info));
	return type;
}

template <class OP>
struct PostgresArrayAppendState {
public:
	PostgresArrayAppendState(idx_t _number_of_dimensions)
	    : count(0), expected_values(1), datums(nullptr), nulls(nullptr), dimensions(nullptr), lower_bounds(nullptr),
	      number_of_dimensions(_number_of_dimensions) {
		dimensions = (int *)palloc(number_of_dimensions * sizeof(int));
		lower_bounds = (int *)palloc(number_of_dimensions * sizeof(int));
		for (idx_t i = 0; i < number_of_dimensions; i++) {
			// Initialize everything at -1 to indicate that it isn't set yet
			dimensions[i] = -1;
		}
		for (idx_t i = 0; i < number_of_dimensions; i++) {
			// Lower bounds have no significance for us
			lower_bounds[i] = 1;
		}
	}

private:
	static inline const duckdb::vector<duckdb::Value> &
	GetChildren(const duckdb::Value &value) {
		switch (value.type().InternalType()) {
		case duckdb::PhysicalType::LIST:
			return duckdb::ListValue::GetChildren(value);
		case duckdb::PhysicalType::ARRAY:
			return duckdb::ArrayValue::GetChildren(value);
		default:
			throw duckdb::InvalidInputException("Expected a LIST or ARRAY type, got '%s' instead",
			                                    value.type().ToString());
		}
	}

public:
	void
	AppendValueAtDimension(const duckdb::Value &value, idx_t dimension) {
		auto &values = GetChildren(value);

		if (values.size() > PG_INT32_MAX) {
			throw duckdb::InvalidInputException("Too many values (%llu) at dimension %d: would overflow", values.size(),
			                                    dimension);
		}

		int32_t to_append = values.size();

		D_ASSERT(dimension < number_of_dimensions);
		if (dimensions[dimension] == -1) {
			// This dimension is not set yet
			dimensions[dimension] = to_append;
			expected_values *= to_append;
			if (pg_mul_u64_overflow(expected_values, static_cast<uint64>(to_append), &expected_values)) {
				throw duckdb::InvalidInputException(
				    "Multiplying %d expected values by %d new ones at dimension %d would overflow", expected_values,
				    to_append, dimension);
			}
		}
		if (dimensions[dimension] != to_append) {
			throw duckdb::InvalidInputException("Expected %d values in list at dimension %d, found %d instead",
			                                    dimensions[dimension], dimension, to_append);
		}

		auto &child_type = GetChildType(value.type());
		if (child_type.id() == duckdb::LogicalTypeId::LIST) {
			for (auto &child_val : values) {
				if (child_val.IsNull()) {
					// Postgres arrays can not contains nulls at the array level
					// i.e {{1,2}, NULL, {3,4}} is not supported
					throw duckdb::InvalidInputException("Returned LIST contains a NULL at an intermediate dimension "
					                                    "(not the value level), which is not supported in Postgres");
				}
				AppendValueAtDimension(child_val, dimension + 1);
			}
		} else {
			if (!datums) {
				// First time we get to the outer most child
				// Because we traversed all dimensions we know how many values we have to allocate for
				datums = (Datum *)palloc(expected_values * sizeof(Datum));
				nulls = (bool *)palloc(expected_values * sizeof(bool));
			}

			for (auto &child_val : values) {
				nulls[count] = child_val.IsNull();
				if (!nulls[count]) {
					datums[count] = OP::ConvertToPostgres(child_val);
				}
				++count;
			}
		}
	}

private:
	idx_t count = 0;

public:
	uint64 expected_values = 1;
	Datum *datums = nullptr;
	bool *nulls = nullptr;
	int *dimensions;
	int *lower_bounds;
	idx_t number_of_dimensions;
};

} // namespace

template <class OP>
static void
ConvertDuckToPostgresArray(TupleTableSlot *slot, duckdb::Value &value, idx_t col) {
	D_ASSERT(pgduckdb::IsNestedType(value.type().id()));
	auto number_of_dimensions = GetDuckDBListDimensionality(value.type());

	PostgresArrayAppendState<OP> append_state(number_of_dimensions);
	append_state.AppendValueAtDimension(value, 0);

	// Create the array
	auto datums = append_state.datums;
	auto nulls = append_state.nulls;
	auto dimensions = append_state.dimensions;
	auto lower_bounds = append_state.lower_bounds;

	// When we insert an empty array into multi-dimensions array,
	// the dimensions[1] to dimension[number_of_dimensions-1] will not be set and always be -1.
	for (idx_t i = 0; i < number_of_dimensions; i++) {
		if (dimensions[i] == -1) {
			// This dimension is not set yet, we should set them to 0.
			// Otherwise, it will cause some issues when we call ConstructArray.
			dimensions[i] = 0;
		}
	}

	auto arr = OP::ConstructArray(datums, nulls, number_of_dimensions, dimensions, lower_bounds);

	// Free allocated memory
	if (append_state.expected_values > 0) {
		pfree(datums);
		pfree(nulls);
	}
	pfree(dimensions);
	pfree(lower_bounds);

	slot->tts_values[col] = PointerGetDatum(arr);
}

bool
ConvertDuckToPostgresValue(TupleTableSlot *slot, duckdb::Value &value, idx_t col) {
	Oid oid = TupleDescAttr(slot->tts_tupleDescriptor, col)->atttypid;

	switch (oid) {
	case BITOID:
	case VARBITOID: {
		slot->tts_values[col] = ConvertVarbitDatum(value);
		break;
	}
	case BOOLOID:
		slot->tts_values[col] = ConvertBoolDatum(value);
		break;
	case CHAROID:
		slot->tts_values[col] = ConvertCharDatum(value);
		break;
	case INT2OID: {
		slot->tts_values[col] = ConvertInt2Datum(value);
		break;
	}
	case INT4OID: {
		slot->tts_values[col] = ConvertInt4Datum(value);
		break;
	}
	case INT8OID: {
		slot->tts_values[col] = ConvertInt8Datum(value);
		break;
	}
	case BPCHAROID:
	case TEXTOID:
	case JSONOID:
	case VARCHAROID: {
		slot->tts_values[col] = ConvertToStringDatum(value);
		break;
	}
	case DATEOID: {
		slot->tts_values[col] = ConvertDateDatum(value);
		break;
	}
	case TIMESTAMPOID: {
		slot->tts_values[col] = ConvertTimestampDatum(value);
		break;
	}
	case TIMESTAMPTZOID: {
		slot->tts_values[col] = ConvertTimestampTzDatum(value);
		break;
	}
	case INTERVALOID: {
		slot->tts_values[col] = ConvertIntervalDatum(value);
		break;
	}
	case TIMEOID: {
		slot->tts_values[col] = ConvertTimeDatum(value);
		break;
	}
	case TIMETZOID:
		slot->tts_values[col] = ConvertTimeTzDatum(value);
		break;
	case FLOAT4OID: {
		slot->tts_values[col] = ConvertFloatDatum(value);
		break;
	}
	case FLOAT8OID: {
		slot->tts_values[col] = ConvertDoubleDatum(value);
		break;
	}
	case NUMERICOID: {
		slot->tts_values[col] = ConvertNumericDatum(value);
		break;
	}
	case UUIDOID: {
		slot->tts_values[col] = ConvertUUIDDatum(value);
		break;
	}
	case BYTEAOID: {
		slot->tts_values[col] = ConvertBinaryDatum(value);
		break;
	}
	case BOOLARRAYOID: {
		ConvertDuckToPostgresArray<BoolArray>(slot, value, col);
		break;
	}
	case CHARARRAYOID: {
		ConvertDuckToPostgresArray<CharArray>(slot, value, col);
		break;
	}
	case INT2ARRAYOID: {
		ConvertDuckToPostgresArray<Int2Array>(slot, value, col);
		break;
	}
	case INT4ARRAYOID: {
		ConvertDuckToPostgresArray<Int4Array>(slot, value, col);
		break;
	}
	case INT8ARRAYOID: {
		ConvertDuckToPostgresArray<Int8Array>(slot, value, col);
		break;
	}
	case BPCHARARRAYOID:
	case TEXTARRAYOID:
	case JSONARRAYOID:
	case VARCHARARRAYOID: {
		ConvertDuckToPostgresArray<TextArray>(slot, value, col);
		break;
	}
	case DATEARRAYOID: {
		ConvertDuckToPostgresArray<DateArray>(slot, value, col);
		break;
	}
	case TIMESTAMPARRAYOID: {
		ConvertDuckToPostgresArray<TimestampArray>(slot, value, col);
		break;
	}
	case TIMESTAMPTZARRAYOID: {
		ConvertDuckToPostgresArray<TimestampTzArray>(slot, value, col);
		break;
	}
	case INTERVALARRAYOID: {
		ConvertDuckToPostgresArray<IntervalArray>(slot, value, col);
		break;
	}
	case BITARRAYOID:
	case VARBITARRAYOID: {
		ConvertDuckToPostgresArray<BitArray>(slot, value, col);
		break;
	}
	case TIMEARRAYOID: {
		ConvertDuckToPostgresArray<TimeArray>(slot, value, col);
		break;
	}
	case TIMETZARRAYOID: {
		ConvertDuckToPostgresArray<TimeTzArray>(slot, value, col);
		break;
	}
	case FLOAT4ARRAYOID: {
		ConvertDuckToPostgresArray<Float4Array>(slot, value, col);
		break;
	}
	case FLOAT8ARRAYOID: {
		ConvertDuckToPostgresArray<Float8Array>(slot, value, col);
		break;
	}
	case NUMERICARRAYOID: {
		ConvertDuckToPostgresArray<NumericArray>(slot, value, col);
		break;
	}
	case UUIDARRAYOID: {
		ConvertDuckToPostgresArray<UUIDArray>(slot, value, col);
		break;
	}
	case BYTEAARRAYOID: {
		ConvertDuckToPostgresArray<ByteArray>(slot, value, col);
		break;
	}
	default: {
		// Since oids of the following types calculated at runtime, it is not
		// possible to compile the code while placing it as a separate case
		// in the switch-case clause above.
		if (oid == pgduckdb::DuckdbStructOid()) {
			slot->tts_values[col] = ConvertDuckStructDatum(value);
			return true;
		} else if (oid == pgduckdb::DuckdbUnionOid()) {
			slot->tts_values[col] = ConvertUnionDatum(value);
			return true;
		} else if (oid == pgduckdb::DuckdbMapOid()) {
			slot->tts_values[col] = ConvertMapDatum(value);
			return true;
		} else if (oid == pgduckdb::DuckdbStructArrayOid()) {
			ConvertDuckToPostgresArray<StructArray>(slot, value, col);
			return true;
		} else if (oid == pgduckdb::DuckdbUnionArrayOid()) {
			ConvertDuckToPostgresArray<UnionArray>(slot, value, col);
			return true;
		} else if (oid == pgduckdb::DuckdbMapArrayOid()) {
			ConvertDuckToPostgresArray<MapArray>(slot, value, col);
			return true;
		}
		elog(WARNING, "(PGDuckDB/ConvertDuckToPostgresValue) Unsuported pgduckdb type: %d", oid);
		return false;
	}
	}
	return true;
}

static inline int32
make_numeric_typmod(int precision, int scale) {
	return ((precision << 16) | (scale & 0x7ff)) + VARHDRSZ;
}

static inline int
numeric_typmod_precision(int32 typmod) {
	return ((typmod - VARHDRSZ) >> 16) & 0xffff;
}

static inline int
numeric_typmod_scale(int32 typmod) {
	return (((typmod - VARHDRSZ) & 0x7ff) ^ 1024) - 1024;
}

duckdb::LogicalType
ConvertPostgresToBaseDuckColumnType(Form_pg_attribute &attribute) {
	int32 type_modifier = attribute->atttypmod;
	Oid typoid = pg::GetBaseTypeAndTypmod(attribute->atttypid, &type_modifier);
	switch (typoid) {
	case BOOLOID:
	case BOOLARRAYOID:
		return duckdb::LogicalTypeId::BOOLEAN;
	case CHAROID:
	case CHARARRAYOID:
		return duckdb::LogicalTypeId::TINYINT;
	case INT2OID:
	case INT2ARRAYOID:
		return duckdb::LogicalTypeId::SMALLINT;
	case INT4OID:
	case INT4ARRAYOID:
		return duckdb::LogicalTypeId::INTEGER;
	case INT8OID:
	case INT8ARRAYOID:
		return duckdb::LogicalTypeId::BIGINT;
	case BPCHAROID:
	case BPCHARARRAYOID:
	case TEXTOID:
	case TEXTARRAYOID:
	case VARCHAROID:
	case VARCHARARRAYOID:
		return duckdb::LogicalTypeId::VARCHAR;
	case DATEOID:
	case DATEARRAYOID:
		return duckdb::LogicalTypeId::DATE;
	case TIMESTAMPOID:
	case TIMESTAMPARRAYOID:
		return duckdb::LogicalTypeId::TIMESTAMP;
	case TIMESTAMPTZOID:
		return duckdb::LogicalTypeId::TIMESTAMP_TZ;
	case INTERVALOID:
	case INTERVALARRAYOID:
		return duckdb::LogicalTypeId::INTERVAL;
	case BITOID:
	case BITARRAYOID:
	case VARBITOID:
	case VARBITARRAYOID:
		return duckdb::LogicalTypeId::BIT;
	case TIMEOID:
	case TIMEARRAYOID:
		return duckdb::LogicalTypeId::TIME;
	case TIMETZOID:
	case TIMETZARRAYOID:
		return duckdb::LogicalTypeId::TIME_TZ;
	case FLOAT4OID:
	case FLOAT4ARRAYOID:
		return duckdb::LogicalTypeId::FLOAT;
	case FLOAT8OID:
	case FLOAT8ARRAYOID:
		return duckdb::LogicalTypeId::DOUBLE;
	case NUMERICOID:
	case NUMERICARRAYOID: {
		auto precision = numeric_typmod_precision(type_modifier);
		auto scale = numeric_typmod_scale(type_modifier);

		/*
		 * DuckDB decimals only support up to 38 digits. So we cannot convert
		 * NUMERICs of higher precision losslessly. We do allow conversion to
		 * doubles.
		 * https://duckdb.org/docs/stable/sql/data_types/numeric.html#fixed-point-decimals
		 */
		if (type_modifier == -1 || precision < 1 || precision > 38 || scale < 0 || scale > 38 || scale > precision) {
			if (duckdb_convert_unsupported_numeric_to_double) {
				auto extra_type_info = duckdb::make_shared_ptr<NumericAsDouble>();
				return duckdb::LogicalType(duckdb::LogicalTypeId::DOUBLE, std::move(extra_type_info));
			}

			/* We don't allow conversion then! */
			if (type_modifier == -1) {
				return CreateUnsupportedPostgresType(
				    "DuckDB requires the precision of a NUMERIC to be set. You can choose to convert these NUMERICs to "
				    "a DOUBLE by using 'SET duckdb.convert_unsupported_numeric_to_double = true'");
			} else if (precision < 1 || precision > 38) {
				return CreateUnsupportedPostgresType(
				    "DuckDB only supports NUMERIC with a precision of 1-38. You can choose to convert these NUMERICs "
				    "to a DOUBLE by using 'SET duckdb.convert_unsupported_numeric_to_double = true'");
			} else if (scale < 0 || scale > 38) {
				return CreateUnsupportedPostgresType(
				    "DuckDB only supports NUMERIC with a scale of 0-38. You can choose to convert these NUMERICs to a "
				    "DOUBLE by using 'SET duckdb.convert_unsupported_numeric_to_double = true'");
			} else {
				return CreateUnsupportedPostgresType(
				    "DuckDB does not support NUMERIC with a scale that is larger than the precision. You can choose to "
				    "convert these NUMERICs to a DOUBLE by using 'SET duckdb.convert_unsupported_numeric_to_double = "
				    "true'");
			}
		}

		return duckdb::LogicalType::DECIMAL(precision, scale);
	}
	case UUIDOID:
	case UUIDARRAYOID:
		return duckdb::LogicalTypeId::UUID;
	case JSONOID:
	case JSONARRAYOID:
	case JSONBOID:
	case JSONBARRAYOID:
		return duckdb::LogicalType::JSON();
	case REGCLASSOID:
	case REGCLASSARRAYOID:
		return duckdb::LogicalTypeId::UINTEGER;
	case BYTEAOID:
	case BYTEAARRAYOID:
		return duckdb::LogicalTypeId::BLOB;
	default:
		if (typoid == pgduckdb::DuckdbUnionOid()) {
			return duckdb::LogicalTypeId::UNION;
		} else if (typoid == pgduckdb::DuckdbStructOid()) {
			return duckdb::LogicalTypeId::STRUCT;
		} else if (typoid == pgduckdb::DuckdbMapOid()) {
			return duckdb::LogicalTypeId::MAP;
		} else if (typoid == pgduckdb::DuckdbUnionArrayOid()) {
			return duckdb::LogicalTypeId::UNION;
		} else if (typoid == pgduckdb::DuckdbStructArrayOid()) {
			return duckdb::LogicalTypeId::STRUCT;
		} else if (typoid == pgduckdb::DuckdbMapArrayOid()) {
			return duckdb::LogicalTypeId::MAP;
		}
		return CreateUnsupportedPostgresType("Oid=" + std::to_string(attribute->atttypid));
	}
}

duckdb::LogicalType
ConvertPostgresToDuckColumnType(Form_pg_attribute &attribute) {
	auto base_type = ConvertPostgresToBaseDuckColumnType(attribute);
	if (base_type.id() == duckdb::LogicalTypeId::INVALID) {
		return base_type;
	}

	if (!pg::IsArrayType(attribute->atttypid)) {
		if (!pg::IsArrayDomainType(attribute->atttypid)) {
			return base_type;
		}
	}

	auto dimensions = attribute->attndims;

	/*
	 * Multi-dimensional arrays in Postgres and nested lists in DuckDB are
	 * quite different in behaviour. We try to map them to eachother anyway,
	 * because in a lot of cases that works fine. But there's also quite a few
	 * where users will get errors.
	 *
	 * To support multi-dimensional arrays that are stored in Postgres tables,
	 * we assume that the attndims value is correct. If people have specified
	 * the matching number of [] when creating the table, that is the case.
	 * It's even possible to store arrays of different dimensions in a single
	 * column. DuckDB does not support that.
	 *
	 * In certain cases (such as tables created by a CTAS) attndims can even be
	 * 0 for array types. It's impossible for us to find out what the actual
	 * dimensions are without reading the first row. Given that it's most
	 * to use single-dimensional arrays, we assume that such a column stores
	 * those.
	 */
	if (dimensions == 0) {
		dimensions = 1;
	}

	for (int i = 0; i < dimensions; i++) {
		base_type = duckdb::LogicalType::LIST(base_type);
	}
	return base_type;
}

Oid
GetPostgresArrayDuckDBType(const duckdb::LogicalType &type, bool throw_error) {
	switch (type.id()) {
	case duckdb::LogicalTypeId::BOOLEAN:
		return BOOLARRAYOID;
	case duckdb::LogicalTypeId::TINYINT:
		return INT2ARRAYOID;
	case duckdb::LogicalTypeId::SMALLINT:
		return INT2ARRAYOID;
	case duckdb::LogicalTypeId::INTEGER:
		return INT4ARRAYOID;
	case duckdb::LogicalTypeId::BIGINT:
		return INT8ARRAYOID;
	case duckdb::LogicalTypeId::HUGEINT:
		return NUMERICARRAYOID;
	case duckdb::LogicalTypeId::UTINYINT:
		return INT2ARRAYOID;
	case duckdb::LogicalTypeId::USMALLINT:
		return INT4ARRAYOID;
	case duckdb::LogicalTypeId::UINTEGER:
		return INT8ARRAYOID;
	case duckdb::LogicalTypeId::VARCHAR:
		return type.IsJSONType() ? JSONARRAYOID : TEXTARRAYOID;
	case duckdb::LogicalTypeId::DATE:
		return DATEARRAYOID;
	case duckdb::LogicalTypeId::TIMESTAMP:
		return TIMESTAMPARRAYOID;
	case duckdb::LogicalTypeId::TIMESTAMP_TZ:
		return TIMESTAMPTZARRAYOID;
	case duckdb::LogicalTypeId::INTERVAL:
		return INTERVALARRAYOID;
	case duckdb::LogicalTypeId::BIT:
		return VARBITARRAYOID;
	case duckdb::LogicalTypeId::TIME:
		return TIMEARRAYOID;
	case duckdb::LogicalTypeId::TIME_TZ:
		return TIMETZARRAYOID;
	case duckdb::LogicalTypeId::FLOAT:
		return FLOAT4ARRAYOID;
	case duckdb::LogicalTypeId::DOUBLE:
		return FLOAT8ARRAYOID;
	case duckdb::LogicalTypeId::DECIMAL:
		return NUMERICARRAYOID;
	case duckdb::LogicalTypeId::UUID:
		return UUIDARRAYOID;
	case duckdb::LogicalTypeId::BLOB:
		return BYTEAARRAYOID;
	case duckdb::LogicalTypeId::BIGNUM:
		return NUMERICARRAYOID;
	case duckdb::LogicalTypeId::STRUCT:
		return pgduckdb::DuckdbStructArrayOid();
	case duckdb::LogicalTypeId::UNION:
		return pgduckdb::DuckdbUnionArrayOid();
	case duckdb::LogicalTypeId::MAP:
		return pgduckdb::DuckdbMapArrayOid();
	default: {
		if (throw_error) {
			throw duckdb::NotImplementedException("Unsupported DuckDB `LIST` subtype: " + type.ToString());
		} else {
			pd_log(WARNING, "Unsupported DuckDB `LIST` subtype: %s", type.ToString().c_str());
			return InvalidOid;
		}
	}
	}
}

// Check if this expression has UnsupportedPostgresType
void
CheckForUnsupportedPostgresType(duckdb::LogicalType type) {
	if (type.id() == duckdb::LogicalTypeId::INVALID && type.GetAlias() == "UnsupportedPostgresType") {
		// Extract and include any modifier information from the type
		auto info = type.GetExtensionInfo();
		if (info && info->modifiers.size() > 0) {
			// Use the first modifier as the error message
			auto modifier_value = info->modifiers[0];
			throw duckdb::NotImplementedException("Unsupported PostgreSQL type found in query: %s",
			                                      modifier_value.ToString());
		} else {
			// Fallback to the alias if no modifiers are available
			throw duckdb::NotImplementedException("Unsupported PostgreSQL type found in query");
		}
	}
}

Oid
GetPostgresDuckDBType(const duckdb::LogicalType &type, bool throw_error) {
	CheckForUnsupportedPostgresType(type);
	switch (type.id()) {
	case duckdb::LogicalTypeId::BOOLEAN:
		return BOOLOID;
	case duckdb::LogicalTypeId::TINYINT:
		return INT2OID;
	case duckdb::LogicalTypeId::SMALLINT:
		return INT2OID;
	case duckdb::LogicalTypeId::INTEGER:
		return INT4OID;
	case duckdb::LogicalTypeId::BIGINT:
		return INT8OID;
	case duckdb::LogicalTypeId::UBIGINT:
	case duckdb::LogicalTypeId::HUGEINT:
	case duckdb::LogicalTypeId::UHUGEINT:
		return NUMERICOID;
	case duckdb::LogicalTypeId::UTINYINT:
		return INT2OID;
	case duckdb::LogicalTypeId::USMALLINT:
		return INT4OID;
	case duckdb::LogicalTypeId::UINTEGER:
		return INT8OID;
	case duckdb::LogicalTypeId::VARCHAR:
		return type.IsJSONType() ? JSONOID : TEXTOID;
	case duckdb::LogicalTypeId::DATE:
		return DATEOID;
	case duckdb::LogicalTypeId::TIMESTAMP:
	case duckdb::LogicalTypeId::TIMESTAMP_SEC:
	case duckdb::LogicalTypeId::TIMESTAMP_MS:
	case duckdb::LogicalTypeId::TIMESTAMP_NS:
		return TIMESTAMPOID;
	case duckdb::LogicalTypeId::TIMESTAMP_TZ:
		return TIMESTAMPTZOID;
	case duckdb::LogicalTypeId::INTERVAL:
		return INTERVALOID;
	case duckdb::LogicalTypeId::BIT:
		return VARBITOID;
	case duckdb::LogicalTypeId::TIME:
		return TIMEOID;
	case duckdb::LogicalTypeId::TIME_TZ:
		return TIMETZOID;
	case duckdb::LogicalTypeId::FLOAT:
		return FLOAT4OID;
	case duckdb::LogicalTypeId::DOUBLE:
		return FLOAT8OID;
	case duckdb::LogicalTypeId::DECIMAL:
		return NUMERICOID;
	case duckdb::LogicalTypeId::UUID:
		return UUIDOID;
	case duckdb::LogicalTypeId::BIGNUM:
		return NUMERICOID;
	case duckdb::LogicalTypeId::STRUCT:
		return pgduckdb::DuckdbStructOid();
	case duckdb::LogicalTypeId::LIST:
	case duckdb::LogicalTypeId::ARRAY: {
		const duckdb::LogicalType *duck_type = &type;
		while (IsNestedType(duck_type->id())) {
			auto &child_type = pgduckdb::GetChildType(*duck_type);
			duck_type = &child_type;
		}
		return GetPostgresArrayDuckDBType(*duck_type, throw_error);
	}
	case duckdb::LogicalTypeId::BLOB:
		return BYTEAOID;
	case duckdb::LogicalTypeId::UNION:
		return pgduckdb::DuckdbUnionOid();
	case duckdb::LogicalTypeId::MAP:
		return pgduckdb::DuckdbMapOid();
	case duckdb::LogicalTypeId::ENUM:
		return VARCHAROID;
	default: {
		if (throw_error) {
			throw duckdb::NotImplementedException("Could not convert DuckDB type: " + type.ToString() +
			                                      " to Postgres type");
		} else {
			pd_log(WARNING, "Could not convert DuckDB type: %s to Postgres type", type.ToString().c_str());
			return InvalidOid;
		}
	}
	}
}

int32
GetPostgresDuckDBTypemod(const duckdb::LogicalType &type) {
	switch (type.id()) {
	case duckdb::LogicalTypeId::DECIMAL: {
		uint8_t width, scale;
		type.GetDecimalProperties(width, scale);
		return make_numeric_typmod(width, scale);
	}
	default:
		return -1;
	}
}

template <class T>
static void
Append(duckdb::Vector &result, T value, idx_t offset) {
	auto data = duckdb::FlatVector::GetData<T>(result);
	data[offset] = value;
}

static void
AppendString(duckdb::Vector &result, Datum value, idx_t offset, bool is_bpchar) {
	const char *text = VARDATA_ANY(value);
	/* Remove the padding of a BPCHAR type. DuckDB expects unpadded value. */
	auto len = is_bpchar ? bpchartruelen(VARDATA_ANY(value), VARSIZE_ANY_EXHDR(value)) : VARSIZE_ANY_EXHDR(value);
	duckdb::string_t str(text, len);

	auto data = duckdb::FlatVector::GetData<duckdb::string_t>(result);
	data[offset] = duckdb::StringVector::AddString(result, str);
}

static void
AppendJsonb(duckdb::Vector &result, Datum value, idx_t offset) {
	auto jsonb = DatumGetJsonbP(value);
	StringInfo str = PostgresFunctionGuard(makeStringInfo);
	auto json_str = PostgresFunctionGuard(JsonbToCString, str, &jsonb->root, VARSIZE(jsonb));
	auto data = duckdb::FlatVector::GetData<duckdb::string_t>(result);
	data[offset] = duckdb::StringVector::AddString(result, json_str, str->len);
}

static void
AppendDate(duckdb::Vector &result, Datum value, idx_t offset) {
	auto date = DatumGetDateADT(value);
	if (date == DATEVAL_NOBEGIN) {
		// -infinity value is different between PG and duck
		Append<duckdb::date_t>(result, duckdb::date_t::ninfinity(), offset);
		return;
	}
	if (date == DATEVAL_NOEND) {
		Append<duckdb::date_t>(result, duckdb::date_t::infinity(), offset);
		return;
	}

	Append<duckdb::date_t>(result, duckdb::date_t(static_cast<int32_t>(date + PGDUCKDB_DUCK_DATE_OFFSET)), offset);
}

static void
AppendTimestamp(duckdb::Vector &result, Datum value, idx_t offset) {
	int64_t timestamp = static_cast<int64_t>(DatumGetTimestamp(value));
	if (timestamp == DT_NOBEGIN) {
		// -infinity value is different between PG and duck
		Append<duckdb::timestamp_t>(result, duckdb::timestamp_t::ninfinity(), offset);
		return;
	}
	if (timestamp == DT_NOEND) {
		Append<duckdb::timestamp_t>(result, duckdb::timestamp_t::infinity(), offset);
		return;
	}

	// Bounds Check
	if (!ValidTimestampOrTimestampTz(timestamp + PGDUCKDB_DUCK_TIMESTAMP_OFFSET))
		throw duckdb::OutOfRangeException(
		    "The Timestamp value should be between min and max value (%s <-> %s)",
		    duckdb::Timestamp::ToString(static_cast<duckdb::timestamp_t>(PGDUCKDB_MIN_TIMESTAMP_VALUE)),
		    duckdb::Timestamp::ToString(static_cast<duckdb::timestamp_t>(PGDUCKDB_MAX_TIMESTAMP_VALUE)));

	Append<duckdb::timestamp_t>(result, duckdb::timestamp_t(timestamp + PGDUCKDB_DUCK_TIMESTAMP_OFFSET), offset);
}

static void
AppendTimestampTz(duckdb::Vector &result, Datum value, idx_t offset) {
	int64_t timestamp = static_cast<int64_t>(DatumGetTimestampTz(value));
	if (timestamp == DT_NOBEGIN) {
		// -infinity value is different between PG and duck
		Append<duckdb::timestamp_tz_t>(result, static_cast<duckdb::timestamp_tz_t>(duckdb::timestamp_t::ninfinity()),
		                               offset);
		return;
	}
	if (timestamp == DT_NOEND) {
		Append<duckdb::timestamp_tz_t>(result, static_cast<duckdb::timestamp_tz_t>(duckdb::timestamp_t::infinity()),
		                               offset);
		return;
	}

	// Bounds Check
	if (!ValidTimestampOrTimestampTz(timestamp + PGDUCKDB_DUCK_TIMESTAMP_OFFSET))
		throw duckdb::OutOfRangeException(
		    "The TimestampTz value should be between min and max value (%s <-> %s)",
		    duckdb::Timestamp::ToString(static_cast<duckdb::timestamp_tz_t>(PGDUCKDB_MIN_TIMESTAMP_VALUE)),
		    duckdb::Timestamp::ToString(static_cast<duckdb::timestamp_tz_t>(PGDUCKDB_MAX_TIMESTAMP_VALUE)));

	Append<duckdb::timestamp_tz_t>(result, duckdb::timestamp_tz_t(timestamp + PGDUCKDB_DUCK_TIMESTAMP_OFFSET), offset);
}

template <class T, class OP = DecimalConversionInteger>
T
ConvertDecimal(const NumericVar &numeric) {
	auto scale_POWER = OP::GetPowerOfTen(numeric.dscale);

	if (numeric.ndigits == 0) {
		return 0;
	}

	T integral_part = 0, fractional_part = 0;
	if (numeric.weight >= 0) {
		int32_t digit_index = 0;
		integral_part = numeric.digits[digit_index++];
		for (; digit_index <= numeric.weight; digit_index++) {
			integral_part *= NBASE;
			if (digit_index < numeric.ndigits) {
				integral_part += numeric.digits[digit_index];
			}
		}
		integral_part *= scale_POWER;
	}

	// we need to find out how large the fractional part is in terms of powers
	// of ten this depends on how many times we multiplied with NBASE
	// if that is different from scale, we need to divide the extra part away
	// again
	// similarly, if trailing zeroes have been suppressed, we have not been multiplying t
	// the fractional part with NBASE often enough. If so, add additional powers
	if (numeric.ndigits > numeric.weight + 1) {
		auto fractional_power = (numeric.ndigits - numeric.weight - 1) * DEC_DIGITS;
		auto fractional_power_correction = fractional_power - numeric.dscale;
		D_ASSERT(fractional_power_correction < 20);
		fractional_part = 0;
		for (int32_t i = duckdb::MaxValue<int32_t>(0, numeric.weight + 1); i < numeric.ndigits; i++) {
			if (i + 1 < numeric.ndigits) {
				// more digits remain - no need to compensate yet
				fractional_part *= NBASE;
				fractional_part += numeric.digits[i];
			} else {
				// last digit, compensate
				T final_base = NBASE;
				T final_digit = numeric.digits[i];
				if (fractional_power_correction >= 0) {
					T compensation = OP::GetPowerOfTen(fractional_power_correction);
					final_base /= compensation;
					final_digit /= compensation;
				} else {
					T compensation = OP::GetPowerOfTen(-fractional_power_correction);
					final_base *= compensation;
					final_digit *= compensation;
				}
				fractional_part *= final_base;
				fractional_part += final_digit;
			}
		}
	}

	// finally
	auto base_res = OP::Finalize(numeric, integral_part + fractional_part);
	return numeric.sign == NUMERIC_NEG ? -base_res : base_res;
}

/*
 * Convert a Postgres Datum to a DuckDB Value. This is meant to be used to
 * covert query parameters in a prepared statement to its DuckDB equivalent.
 * Passing it a Datum that is stored on disk results in undefined behavior,
 * because this fuction makes no effert to detoast the Datum.
 */
duckdb::Value
ConvertPostgresParameterToDuckValue(Datum value, Oid postgres_type) {
	switch (postgres_type) {
	case BOOLOID:
		return duckdb::Value::BOOLEAN(DatumGetBool(value));
	case INT2OID:
		return duckdb::Value::SMALLINT(DatumGetInt16(value));
	case INT4OID:
		return duckdb::Value::INTEGER(DatumGetInt32(value));
	case INT8OID:
		return duckdb::Value::BIGINT(DatumGetInt64(value));
	case BPCHAROID:
	case TEXTOID:
	case JSONOID:
	case VARCHAROID: {
		// FIXME: TextDatumGetCstring allocates so it needs a
		// guard, but it's a macro not a function, so our current gaurd
		// template does not handle it.
		return duckdb::Value(TextDatumGetCString(value));
	}
	case DATEOID:
		return duckdb::Value::DATE(duckdb::date_t(DatumGetDateADT(value) + PGDUCKDB_DUCK_DATE_OFFSET));
	case TIMESTAMPOID:
		return duckdb::Value::TIMESTAMP(duckdb::timestamp_t(DatumGetTimestamp(value) + PGDUCKDB_DUCK_TIMESTAMP_OFFSET));
	case TIMESTAMPTZOID:
		return duckdb::Value::TIMESTAMPTZ(
		    duckdb::timestamp_tz_t(DatumGetTimestampTz(value) + PGDUCKDB_DUCK_TIMESTAMP_OFFSET));
	case INTERVALOID:
		return duckdb::Value::INTERVAL(DatumGetInterval(value));
	case BITOID:
	case VARBITOID:
		return duckdb::Value::BIT(DatumGetBitString(value));
	case TIMEOID:
		return duckdb::Value::TIME(DatumGetTime(value));
	case TIMETZOID:
		return duckdb::Value::TIMETZ(DatumGetTimeTz(value));
	case FLOAT4OID:
		return duckdb::Value::FLOAT(DatumGetFloat4(value));
	case FLOAT8OID:
		return duckdb::Value::DOUBLE(DatumGetFloat8(value));
	case UUIDOID:
		return duckdb::Value::UUID(DatumGetUUID(value));
	default:
		elog(ERROR, "Could not convert Postgres parameter of type: %d to DuckDB type", postgres_type);
	}
}

void
ConvertPostgresToDuckValue(Oid attr_type, Datum value, duckdb::Vector &result, idx_t offset) {
	auto &type = result.GetType();
	switch (type.id()) {
	case duckdb::LogicalTypeId::BOOLEAN:
		Append<bool>(result, DatumGetBool(value), offset);
		break;
	case duckdb::LogicalTypeId::TINYINT: {
		Append<int16_t>(result, DatumGetInt16(value), offset);
		break;
	}
	case duckdb::LogicalTypeId::SMALLINT:
		Append<int16_t>(result, DatumGetInt16(value), offset);
		break;
	case duckdb::LogicalTypeId::INTEGER:
		Append<int32_t>(result, DatumGetInt32(value), offset);
		break;
	case duckdb::LogicalTypeId::UINTEGER:
		Append<uint32_t>(result, DatumGetUInt32(value), offset);
		break;
	case duckdb::LogicalTypeId::BIGINT:
		Append<int64_t>(result, DatumGetInt64(value), offset);
		break;
	case duckdb::LogicalTypeId::VARCHAR: {
		// NOTE: This also handles JSON
		if (attr_type == JSONBOID) {
			AppendJsonb(result, value, offset);
			break;
		} else {
			AppendString(result, value, offset, attr_type == BPCHAROID);
		}
		break;
	}
	case duckdb::LogicalTypeId::DATE:
		AppendDate(result, value, offset);
		break;

	case duckdb::LogicalTypeId::TIMESTAMP_SEC:
	case duckdb::LogicalTypeId::TIMESTAMP_MS:
	case duckdb::LogicalTypeId::TIMESTAMP_NS:
	case duckdb::LogicalTypeId::TIMESTAMP:
		AppendTimestamp(result, value, offset);
		break;
	case duckdb::LogicalTypeId::TIMESTAMP_TZ:
		AppendTimestampTz(result, value, offset); // Timestamp and Timestamptz are basically same in PG
		break;
	case duckdb::LogicalTypeId::INTERVAL:
		Append<duckdb::interval_t>(result, DatumGetInterval(value), offset);
		break;
	case duckdb::LogicalTypeId::BIT:
		Append<duckdb::bitstring_t>(result, duckdb::Bit::ToBit(DatumGetBitString(value)), offset);
		break;
	case duckdb::LogicalTypeId::TIME:
		Append<duckdb::dtime_t>(result, DatumGetTime(value), offset);
		break;
	case duckdb::LogicalTypeId::TIME_TZ:
		Append<duckdb::dtime_tz_t>(result, DatumGetTimeTz(value), offset);
		break;
	case duckdb::LogicalTypeId::FLOAT:
		Append<float>(result, DatumGetFloat4(value), offset);
		break;
	case duckdb::LogicalTypeId::DOUBLE: {
		auto aux_info = type.GetAuxInfoShrPtr();
		if (aux_info && dynamic_cast<NumericAsDouble *>(aux_info.get())) {
			// This NUMERIC could not be converted to a DECIMAL, convert it as DOUBLE instead
			auto numeric = DatumGetNumeric(value);
			auto numeric_var = FromNumeric(numeric);
			auto double_val = ConvertDecimal<double, DecimalConversionDouble>(numeric_var);
			Append<double>(result, double_val, offset);
		} else {
			Append<double>(result, DatumGetFloat8(value), offset);
		}
		break;
	}
	case duckdb::LogicalTypeId::DECIMAL: {
		auto physical_type = type.InternalType();
		auto numeric = DatumGetNumeric(value);
		auto numeric_var = FromNumeric(numeric);
		switch (physical_type) {
		case duckdb::PhysicalType::INT16: {
			Append(result, ConvertDecimal<int16_t>(numeric_var), offset);
			break;
		}
		case duckdb::PhysicalType::INT32: {
			Append(result, ConvertDecimal<int32_t>(numeric_var), offset);
			break;
		}
		case duckdb::PhysicalType::INT64: {
			Append(result, ConvertDecimal<int64_t>(numeric_var), offset);
			break;
		}
		case duckdb::PhysicalType::INT128: {
			Append(result, ConvertDecimal<hugeint_t, DecimalConversionHugeint>(numeric_var), offset);
			break;
		}
		default:
			throw duckdb::InternalException("Unrecognized physical type (%s) for DECIMAL value",
			                                duckdb::EnumUtil::ToString(physical_type));
		}
		break;
	}
	case duckdb::LogicalTypeId::UUID: {
		Append(result, DatumGetUUID(value), offset);
		break;
	}
	case duckdb::LogicalTypeId::BLOB: {
		const char *bytea_data = VARDATA_ANY(value);
		size_t bytea_length = VARSIZE_ANY_EXHDR(value);
		const duckdb::string_t s(bytea_data, bytea_length);
		auto data = duckdb::FlatVector::GetData<duckdb::string_t>(result);
		data[offset] = duckdb::StringVector::AddStringOrBlob(result, s);
		break;
	}
	case duckdb::LogicalTypeId::LIST: {
		// Convert Datum to ArrayType
		auto array = DatumGetArrayTypeP(value);

		auto ndims = ARR_NDIM(array);
		int *dims = ARR_DIMS(array);
		auto elem_type = ARR_ELEMTYPE(array);

		int16 typlen;
		bool typbyval;
		char typalign;
		PostgresFunctionGuard(get_typlenbyvalalign, elem_type, &typlen, &typbyval, &typalign);

		int nelems;
		Datum *elems;
		bool *nulls;
		// Deconstruct the array into Datum elements
		PostgresFunctionGuard(deconstruct_array, array, elem_type, typlen, typbyval, typalign, &elems, &nulls, &nelems);

		if (ndims == -1) {
			throw duckdb::InternalException("Array type has an ndims of -1, so it's actually not an array??");
		}
		// Set the list_entry_t metadata
		duckdb::Vector *vec = &result;
		int write_offset = offset;
		for (int dim = 0; dim < ndims; dim++) {
			auto previous_dimension = dim ? dims[dim - 1] : 1;
			auto dimension = dims[dim];
			if (vec->GetType().id() != duckdb::LogicalTypeId::LIST) {
				throw duckdb::InvalidInputException(
				    "Dimensionality of the schema and the data does not match, data contains more dimensions than the "
				    "amount of dimensions specified by the schema");
			}
			auto child_offset = duckdb::ListVector::GetListSize(*vec);
			auto list_data = duckdb::FlatVector::GetData<duckdb::list_entry_t>(*vec);
			for (int entry = 0; entry < previous_dimension; entry++) {
				list_data[write_offset + entry] = duckdb::list_entry_t(
				    // All lists in a postgres row are enforced to have the same dimension
				    // [[1,2],[2,3,4]] is not allowed, second list has 3 elements instead of 2
				    child_offset + (dimension * entry), dimension);
			}
			auto new_child_size = child_offset + (dimension * previous_dimension);
			duckdb::ListVector::Reserve(*vec, new_child_size);
			duckdb::ListVector::SetListSize(*vec, new_child_size);
			write_offset = child_offset;
			auto &child = duckdb::ListVector::GetEntry(*vec);
			vec = &child;
		}
		if (ndims == 0) {
			D_ASSERT(nelems == 0);
			auto child_offset = duckdb::ListVector::GetListSize(*vec);
			auto list_data = duckdb::FlatVector::GetData<duckdb::list_entry_t>(*vec);
			list_data[write_offset] = duckdb::list_entry_t(child_offset, 0);
			vec = &duckdb::ListVector::GetEntry(*vec);
		} else if (vec->GetType().id() == duckdb::LogicalTypeId::LIST) {
			throw duckdb::InvalidInputException(
			    "Dimensionality of the schema and the data does not match, data contains fewer dimensions than the "
			    "amount of dimensions specified by the schema");
		}

		for (int i = 0; i < nelems; i++) {
			idx_t dest_idx = write_offset + i;
			if (nulls[i]) {
				auto &array_mask = duckdb::FlatVector::Validity(*vec);
				array_mask.SetInvalid(dest_idx);
				continue;
			}
			ConvertPostgresToDuckValue(elem_type, elems[i], *vec, dest_idx);
		}
		break;
	}
	default:
		throw duckdb::NotImplementedException("(DuckDB/ConvertPostgresToDuckValue) Unsupported pgduckdb type: %s",
		                                      result.GetType().ToString().c_str());
	}
}

void
InsertTupleIntoChunk(duckdb::DataChunk &output, PostgresScanLocalState &scan_local_state, TupleTableSlot *slot) {

	auto scan_global_state = scan_local_state.global_state;

	if (scan_global_state->count_tuples_only) {
		/* COUNT(*) returned tuple will have only one value returned as first tuple element. */
		scan_global_state->total_row_count += slot->tts_values[0];
		scan_local_state.output_vector_size += slot->tts_values[0];
		return;
	}
	/* Write tuple columns in output vector. */
	for (int duckdb_output_index = 0; duckdb_output_index < slot->tts_tupleDescriptor->natts; duckdb_output_index++) {
		auto &result = output.data[duckdb_output_index];
		if (slot->tts_isnull[duckdb_output_index]) {
			auto &array_mask = duckdb::FlatVector::Validity(result);
			array_mask.SetInvalid(scan_local_state.output_vector_size);
		} else {
			auto attr = TupleDescAttr(slot->tts_tupleDescriptor, duckdb_output_index);
			if (attr->attlen == -1) {
				bool should_free = false;
				Datum detoasted_value = DetoastPostgresDatum(
				    reinterpret_cast<varlena *>(slot->tts_values[duckdb_output_index]), &should_free);
				ConvertPostgresToDuckValue(attr->atttypid, detoasted_value, result,
				                           scan_local_state.output_vector_size);
				if (should_free) {
					duckdb_free(reinterpret_cast<void *>(detoasted_value));
				}
			} else {
				ConvertPostgresToDuckValue(attr->atttypid, slot->tts_values[duckdb_output_index], result,
				                           scan_local_state.output_vector_size);
			}
		}
	}

	scan_local_state.output_vector_size++;
	scan_global_state->total_row_count++;
}

/*
 * Returns true if the given type can be converted from a Postgres datum to a DuckDB value
 * without requiring any Postgres-specific functions or memory allocations (such as palloc).
 */
static bool
IsThreadSafeTypeForPostgresToDuckDB(Oid attr_type, duckdb::LogicalTypeId duckdb_type) {
	if (duckdb_type == duckdb::LogicalTypeId::VARCHAR) {
		return attr_type != JSONBOID;
	}
	if (duckdb_type == duckdb::LogicalTypeId::LIST || duckdb_type == duckdb::LogicalTypeId::BIT) {
		return false;
	}

	return true;
}

/*
 * Insert batch of tuples into chunk. This function is thread-safe and is meant for multi-threaded scans.
 *
 * Global lock & PG memory context are handled for unsafe types, e.g., JSONB/LIST/VARBIT.
 */
void
InsertTuplesIntoChunk(duckdb::DataChunk &output, PostgresScanLocalState &scan_local_state, TupleTableSlot **slots,
                      int num_slots) {
	if (num_slots == 0) {
		return;
	}

	auto scan_global_state = scan_local_state.global_state;
	int natts = slots[0]->tts_tupleDescriptor->natts;
	D_ASSERT(!scan_global_state->count_tuples_only);

	for (int duckdb_output_index = 0; duckdb_output_index < natts; duckdb_output_index++) {
		auto &result = output.data[duckdb_output_index];
		auto attr = TupleDescAttr(slots[0]->tts_tupleDescriptor, duckdb_output_index);
		bool is_safe_type = IsThreadSafeTypeForPostgresToDuckDB(attr->atttypid, result.GetType().id());

		std::unique_ptr<std::lock_guard<std::recursive_mutex>> lock_guard;
		MemoryContext old_ctx = NULL;
		if (!is_safe_type) {
			lock_guard = std::make_unique<std::lock_guard<std::recursive_mutex>>(GlobalProcessLock::GetLock());
			old_ctx = pg::MemoryContextSwitchTo(scan_global_state->duckdb_scan_memory_ctx);
		}

		for (int row = 0; row < num_slots; row++) {
			if (slots[row]->tts_isnull[duckdb_output_index]) {
				auto &array_mask = duckdb::FlatVector::Validity(result);
				array_mask.SetInvalid(scan_local_state.output_vector_size + row);
			} else {
				if (attr->attlen == -1) {
					bool should_free = false;
					Datum detoasted_value = DetoastPostgresDatum(
					    reinterpret_cast<varlena *>(slots[row]->tts_values[duckdb_output_index]), &should_free);
					ConvertPostgresToDuckValue(attr->atttypid, detoasted_value, result,
					                           scan_local_state.output_vector_size + row);
					if (should_free) {
						duckdb_free(reinterpret_cast<void *>(detoasted_value));
					}
				} else {
					ConvertPostgresToDuckValue(attr->atttypid, slots[row]->tts_values[duckdb_output_index], result,
					                           scan_local_state.output_vector_size + row);
				}
			}
		}

		if (!is_safe_type) {
			pg::MemoryContextSwitchTo(old_ctx);
			pg::MemoryContextReset(scan_global_state->duckdb_scan_memory_ctx);
			// Lock will be automatically unlocked when lock_guard goes out of scope
		}
	}

	scan_local_state.output_vector_size += num_slots;
	scan_global_state->total_row_count += num_slots;
}

NumericVar
FromNumeric(Numeric num) {
	NumericVar dest;
	dest.ndigits = NUMERIC_NDIGITS(num);
	dest.weight = NUMERIC_WEIGHT(num);
	dest.sign = NUMERIC_SIGN(num);
	dest.dscale = NUMERIC_DSCALE(num);
	dest.digits = NUMERIC_DIGITS(num);
	dest.buf = NULL; /* digits array is not palloc'd */
	return dest;
}
} // namespace pgduckdb
