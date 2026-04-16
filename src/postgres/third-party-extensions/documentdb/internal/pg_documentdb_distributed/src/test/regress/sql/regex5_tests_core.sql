CREATE OR REPLACE FUNCTION assert_count_regex5(expected_row_count int, query documentdb_core.bson)
 RETURNS void
 LANGUAGE plpgsql
AS $$
DECLARE
	returned_row_count int;
BEGIN
	SELECT count(*) INTO returned_row_count FROM collection('db','regex5') WHERE document @@ query;

	IF returned_row_count <> expected_row_count THEN
		RAISE 'query % returned % rows instead of %', query, returned_row_count, expected_row_count;
	END IF;
END;
$$;

SELECT insert_one('db','regex5', '{"_id": 1, "x": "ayc"}');
SELECT insert_one('db','regex5', '{"_id": 2, "x": "xValue2"}');
SELECT insert_one('db','regex5', '{"_id": 3, "x": ["abc", "xyz1"]}');
SELECT insert_one('db','regex5', '{"_id": 4, "x": ["acd", "xyz23"]}');
SELECT insert_one('db','regex5', '{"_id": 5, "F1" : "F1_value",  "x": ["first regular expression", "second expression", "third value for x"]}');
SELECT insert_one('db','regex5', '{"_id": 6, "F1" : "F1_value2"}');
SELECT insert_one('db','regex5', '{"_id": 7, "F1" : "this is the new value, done"}');
SELECT insert_one('db','regex5', '{"_id": 8, "F1" : "Giving the flag in the middle of the word \bval\bue\b, done"}');
SELECT insert_one('db','regex5', '{"_id": 9, "F1" : "hi, new y \\yvalue\\y; fine"}');

do $$
DECLARE
	temp text;
begin
	for counter in 1..10 loop
		SELECT insert_one('db','regex5', '{"filler": "fillerValue"}') into temp;
   	end loop;
end
$$;

-- DROP PRIMARY KEY
SELECT documentdb_distributed_test_helpers.drop_primary_key('db', 'regex5');

-- When x is non-array
SELECT assert_count_regex5(1, '{"x": {"$in": [{"$regex" : ".*Yc", "$options": "i"}]}}');

-- When x's value is array and regex matches one of the array elements, specifically the first element (3rd record).
SELECT assert_count_regex5(1, '{"x": {"$in": [{"$regex" : "^.*cd", "$options": ""}]}}');

-- When x's value is array and regex matches second element of 3rd record and 3rd element in the 5th record.
SELECT assert_count_regex5(2, '{"x": {"$in": [{"$regex" : "x.+1", "$options": ""}, {"$regex" : "value .+ x", "$options": ""}]}}');

-- Without any regex
SELECT assert_count_regex5(2, '{"x": {"$in": ["acd", "first regular expression"]}}');

-- Mix of Regex and text
SELECT assert_count_regex5(2, '{"x": {"$in": [  "second expression", {"$regex" : "xy.1", "$options": ""}  ]  }}');

-- Test for hasNull (10 filler records and 4 actual records to match)
SELECT assert_count_regex5(16, '{"x": {"$in": [  "second expression", null, {"$regex" : "xy.1", "$options": ""}  ]  }}');

-- Test for $nin
SELECT assert_count_regex5(18, '{"x": {"$nin": [{"$regex" : ".*yc", "$options": ""}]}}');

-- Test for $nin. With one REGEX and one normal string. 
SELECT assert_count_regex5(18, '{"x": {"$nin": [{"$regex" : ".*b.*c", "$options": ""}, "xyz1", "xValue1"]}}');

-- Test for $nin. Two documents will match the $nin array.
SELECT assert_count_regex5(17, '{"x": {"$nin": [{"$regex" : ".*b.*c", "$options": ""}, "xyz23", "xValue1"]}}');

-- Test for $nin. Single REGEX. 
SELECT assert_count_regex5(17, '{"x": {"$nin": [{"$regex" : "^a.c", "$options": ""}]}}');

-- Test for $nin. with null (only null and single null)
SELECT assert_count_regex5(5, '{"x": {"$nin": [null]}}');

-- Test for $nin. with null (only null and multiple null)
SELECT assert_count_regex5(5, '{"x": {"$nin": [null, null]}}');

-- Test for $nin. with null 
SELECT assert_count_regex5(3, '{"x": {"$nin": [null, "second expression", {"$regex" : "xy.1", "$options": ""}]}}');

-- Test for $all. With all REGEX
SELECT assert_count_regex5(1, '{"x": {"$all": [{"$regex" : "expression", "$options": ""}, {"$regex" : "value .+ x", "$options": ""}]}}');

-- Test for $all. With one REGEX and one normal string.
SELECT assert_count_regex5(1, '{"x": {"$all": [{"$regex" : "^.cd", "$options": ""}, "xyz23"]}}');

-- Test for $all. Empty array
SELECT assert_count_regex5(0, '{"x": {"$all": []}}');

-- Test for $all. Single regex
SELECT assert_count_regex5(1, '{"x": {"$all": [{"$regex": "^.V.+2$", "$options": ""}]}}');

-- Test for $all. Single string and no regex
SELECT assert_count_regex5(1, '{"x": {"$all": ["xValue2"]}}');

-- Test for $all. with null in the $all array
SELECT assert_count_regex5(0, '{"x": {"$all": [null, {"$regex": "xValu.+2$", "$options": ""} ]}}');

-- Test for $all. with only null in the $all array
SELECT assert_count_regex5(14, '{"x": {"$all": [null]}}');

-- Test for $all. with only null in the $all array (with multiple null)
SELECT assert_count_regex5(14, '{"x": {"$all": [null, null]}}');

SELECT assert_count_regex5(4, '{"F1": {"$regex" : "Value", "$options": "i"}}');

select document from collection('db','regex5') where document @@ '{"F1": {"$regex" : "word \bval\bue\b,", "$options": ""}}';

select document from collection('db','regex5') where document @@ '{"F1": { "$regularExpression" : { "pattern" : "word \bval\bue\b,", "options" : "" } } }';

-- Work as word boundary {"F1" : "this is the new value, done"} 
select document from collection('db','regex5') where document @@ '{"F1": { "$regularExpression" : { "pattern" : "\\bValue\\b", "options" : "i" } } }';

-- rec 9
select document from collection('db','regex5') where document @@ '{"F1": { "$regularExpression" : { "pattern" : "\\\\yValue\\\\y", "options" : "i" } } }';
