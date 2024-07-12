SET search_path TO helio_core;

-- numerics share the same hash
SELECT bson_hash_int4('{ "": { "$numberInt": "4" }}');
SELECT bson_hash_int4('{ "": { "$numberLong": "4" }}');
SELECT bson_hash_int4('{ "": 4.0 }');

-- object with numbers still has same hash
SELECT bson_hash_int4('{ "a": { "b": { "$numberInt": "4" }}}');
SELECT bson_hash_int4('{ "a": { "b": { "$numberLong": "4" }}}');
SELECT bson_hash_int4('{ "a": { "b": 4.0 }}');

-- same value code/string/symbol only match for string and symbol
SELECT bson_hash_int4('{ "": "This is a string value"}');
SELECT bson_hash_int4('{ "": { "$symbol": "This is a string value" }}');
SELECT bson_hash_int4('{ "": { "$code": "This is a string value" } }');

-- array of simliar but same values match hash
SELECT bson_hash_int4('{ "a": [ 4.0, "This is a string value" ] }');
SELECT bson_hash_int4('{ "a": [ { "$numberInt": "4" }, { "$symbol" : "This is a string value" }] }');

---------------------------------------------------

SELECT bson_hash_int8('{ "": { "$numberInt": "4" }}', 0);
SELECT bson_hash_int8('{ "": { "$numberLong": "4" }}', 0);
SELECT bson_hash_int8('{ "": 4.0 }', 0);

-- object with numbers still has same hash
SELECT bson_hash_int8('{ "a": { "b": { "$numberInt": "4" }}}', 0);
SELECT bson_hash_int8('{ "a": { "b": { "$numberLong": "4" }}}', 0);
SELECT bson_hash_int8('{ "a": { "b": 4.0 }}', 0);

-- same value code/string/symbol only match for string and symbol
SELECT bson_hash_int8('{ "": "This is a string value"}', 0);
SELECT bson_hash_int8('{ "": { "$symbol": "This is a string value" }}', 0);
SELECT bson_hash_int8('{ "": { "$code": "This is a string value" } }', 0);

-- array of simliar but same values match hash
SELECT bson_hash_int8('{ "a": [ 4.0, "This is a string value" ] }', 0);
SELECT bson_hash_int8('{ "a": [ { "$numberInt": "4" }, { "$symbol" : "This is a string value" }] }', 0);
