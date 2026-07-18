SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 5900000;
SET documentdb.next_collection_id TO 5900;
SET documentdb.next_collection_index_id TO 5900;

-- $hour, $minute, $second, $millisecond: simple
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-01-01T07:30:10.5Z"}}', '{"hour": {"$hour": "$date"}, "minute": {"$minute": "$date"}, "second": {"$second": "$date"}, "ms": {"$millisecond": "$date"}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-01-01T10:30:10.5Z"}}', '{"hour": {"$hour": [ "$date" ]}, "minute": {"$minute": [ "$date" ]}, "second": {"$second": [ "$date" ]}, "ms": {"$millisecond": [ "$date" ]}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-01-01T23:59:59.28Z"}}', '{"hour": {"$hour": { "date": "$date" }}, "minute": {"$minute": { "date": "$date" }}, "second": {"$second": { "date": "$date" }}, "ms": {"$millisecond": { "date": "$date" }}}');
-- 2022-12-14T01:28:46.000Z
SELECT * FROM bson_dollar_project('{"date": {"$oid" : "639926cee6bda3127f153bf1" } }', '{"hour": {"$hour": { "date": "$date" }}, "minute": {"$minute": { "date": "$date" }}, "second": {"$second": { "date": "$date" }}, "ms": {"$millisecond": { "date": "$date" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$timestamp" : { "t": 1670981326, "i": 1 } } }', '{"hour": {"$hour": { "date": "$date" }}, "minute": {"$minute": { "date": "$date" }}, "second": {"$second": { "date": "$date" }}, "ms": {"$millisecond": { "date": "$date" }}}');
-- 2022-12-14T01:31:47.000Z
SELECT * FROM bson_dollar_project('{"date": {"$oid" : "63992783f5e4d92817ef6ccc" } }', '{"hour": {"$hour": { "date": "$date" }}, "minute": {"$minute": { "date": "$date" }}, "second": {"$second": { "date": "$date" }}, "ms": {"$millisecond": { "date": "$date" }}}');

-- $hour, $minute, $second, $millisecond: with timezone
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-01-01T00:00:01.000Z"}, "tz": "+08:05"}', '{"hour": {"$hour": { "date": "$date",  "timezone":  "$tz" }}, "minute": {"$minute": { "date": "$date",  "timezone":  "$tz" }}, "second": {"$second": { "date": "$date",  "timezone":  "$tz" }}, "ms": {"$millisecond": { "date": "$date",  "timezone":  "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-01-01T00:00:00.001Z"}, "tz": "-08:05"}', '{"hour": {"$hour": { "date": "$date",  "timezone":  "$tz" }}, "minute": {"$minute": { "date": "$date",  "timezone":  "$tz" }}, "second": {"$second": { "date": "$date",  "timezone":  "$tz" }}, "ms": {"$millisecond": { "date": "$date",  "timezone":  "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-01-01T00:00:01.000Z"}, "tz": "-08"}', '{"hour": {"$hour": { "date": "$date",  "timezone":  "$tz" }}, "minute": {"$minute": { "date": "$date",  "timezone":  "$tz" }}, "second": {"$second": { "date": "$date",  "timezone":  "$tz" }}, "ms": {"$millisecond": { "date": "$date",  "timezone":  "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-01-01T00:00:01.000Z"}, "tz": "-0801"}', '{"hour": {"$hour": { "date": "$date",  "timezone":  "$tz" }}, "minute": {"$minute": { "date": "$date",  "timezone":  "$tz" }}, "second": {"$second": { "date": "$date",  "timezone":  "$tz" }}, "ms": {"$millisecond": { "date": "$date",  "timezone":  "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-01-01T00:00:01.020Z"}, "tz": "+1210"}', '{"hour": {"$hour": { "date": "$date",  "timezone":  "$tz" }}, "minute": {"$minute": { "date": "$date",  "timezone":  "$tz" }}, "second": {"$second": { "date": "$date",  "timezone":  "$tz" }}, "ms": {"$millisecond": { "date": "$date",  "timezone":  "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-01-01T00:00:01.000Z"}, "tz": "UTC"}', '{"hour": {"$hour": { "date": "$date",  "timezone":  "$tz" }}, "minute": {"$minute": { "date": "$date",  "timezone":  "$tz" }}, "second": {"$second": { "date": "$date",  "timezone":  "$tz" }}, "ms": {"$millisecond": { "date": "$date",  "timezone":  "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-01-01T00:00:01.000Z"}, "tz": "+00"}', '{"hour": {"$hour": { "date": "$date",  "timezone":  "$tz" }}, "minute": {"$minute": { "date": "$date",  "timezone":  "$tz" }}, "second": {"$second": { "date": "$date",  "timezone":  "$tz" }}, "ms": {"$millisecond": { "date": "$date",  "timezone":  "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-01-01T00:00:01.000Z"}, "tz": "+0000"}', '{"hour": {"$hour": { "date": "$date",  "timezone":  "$tz" }}, "minute": {"$minute": { "date": "$date",  "timezone":  "$tz" }}, "second": {"$second": { "date": "$date",  "timezone":  "$tz" }}, "ms": {"$millisecond": { "date": "$date",  "timezone":  "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-01-01T00:00:01.000Z"}, "tz": "-00:00"}', '{"hour": {"$hour": { "date": "$date",  "timezone":  "$tz" }}, "minute": {"$minute": { "date": "$date",  "timezone":  "$tz" }}, "second": {"$second": { "date": "$date",  "timezone":  "$tz" }}, "ms": {"$millisecond": { "date": "$date",  "timezone":  "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-01-01T00:00:01.201Z"}, "tz": "US/Pacific"}', '{"hour": {"$hour": { "date": "$date",  "timezone":  "$tz" }}, "minute": {"$minute": { "date": "$date",  "timezone":  "$tz" }}, "second": {"$second": { "date": "$date",  "timezone":  "$tz" }}, "ms": {"$millisecond": { "date": "$date",  "timezone":  "$tz" }}}');

-- should observe daylight savings or not
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-06-01T00:00:01.201Z"}, "tz": "US/Pacific"}', '{"hour": {"$hour": { "date": "$date",  "timezone":  "$tz" }}, "minute": {"$minute": { "date": "$date",  "timezone":  "$tz" }}, "second": {"$second": { "date": "$date",  "timezone":  "$tz" }}, "ms": {"$millisecond": { "date": "$date",  "timezone":  "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-12-01T00:00:01.201Z"}, "tz": "US/Pacific"}', '{"hour": {"$hour": { "date": "$date",  "timezone":  "$tz" }}, "minute": {"$minute": { "date": "$date",  "timezone":  "$tz" }}, "second": {"$second": { "date": "$date",  "timezone":  "$tz" }}, "ms": {"$millisecond": { "date": "$date",  "timezone":  "$tz" }}}');

-- $year, $month, $dayOfMonth: simple
SELECT * FROM bson_dollar_project('{"date": {"$date": "2005-01-01T07:30:10.5Z"}}', '{"year": {"$year": "$date"}, "month": {"$month": "$date"}, "dayOfMonth": {"$dayOfMonth": "$date"}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2999-02-28T10:30:10.5Z"}}', '{"year": {"$year": [ "$date" ]}, "month": {"$month": [ "$date" ]}, "dayOfMonth": {"$dayOfMonth": [ "$date" ]}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "1930-12-31T23:59:59.28Z"}}', '{"year": {"$year": { "date": "$date" }}, "month": {"$month": { "date": "$date" }}, "dayOfMonth": {"$dayOfMonth": { "date": "$date" }}}');
-- 2022-12-14T01:28:46.000Z
SELECT * FROM bson_dollar_project('{"date": {"$oid" : "639926cee6bda3127f153bf1" }}', '{"year": {"$year": { "date": "$date" }}, "month": {"$month": { "date": "$date" }}, "dayOfMonth": {"$dayOfMonth": { "date": "$date" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$timestamp" : { "t": 1670981326, "i": 1 } }}', '{"year": {"$year": { "date": "$date" }}, "month": {"$month": { "date": "$date" }}, "dayOfMonth": {"$dayOfMonth": { "date": "$date" }}}');

-- $year, $month, $dayOfMonth: with timezone
SELECT * FROM bson_dollar_project('{"date": {"$date": "1930-12-31T23:59:59.28Z"}, "tz": "+01"}', '{"year": {"$year": { "date": "$date", "timezone": "$tz" }}, "month": {"$month": { "date": "$date", "timezone": "$tz" }}, "dayOfMonth": {"$dayOfMonth": { "date": "$date", "timezone": "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "1930-12-31T11:59:00.000Z"}, "tz": "+12"}', '{"year": {"$year": { "date": "$date", "timezone": "$tz" }}, "month": {"$month": { "date": "$date", "timezone": "$tz" }}, "dayOfMonth": {"$dayOfMonth": { "date": "$date", "timezone": "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "1930-12-31T11:59:00.000Z"}, "tz": "+12:01"}', '{"year": {"$year": { "date": "$date", "timezone": "$tz" }}, "month": {"$month": { "date": "$date", "timezone": "$tz" }}, "dayOfMonth": {"$dayOfMonth": { "date": "$date", "timezone": "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "1930-12-31T11:59:00.000Z"}, "tz": "-1159"}', '{"year": {"$year": { "date": "$date", "timezone": "$tz" }}, "month": {"$month": { "date": "$date", "timezone": "$tz" }}, "dayOfMonth": {"$dayOfMonth": { "date": "$date", "timezone": "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "1930-12-31T11:59:00.000Z"}, "tz": "-12"}', '{"year": {"$year": { "date": "$date", "timezone": "$tz" }}, "month": {"$month": { "date": "$date", "timezone": "$tz" }}, "dayOfMonth": {"$dayOfMonth": { "date": "$date", "timezone": "$tz" }}}');

-- $week and $isoWeek 
-- $week range is 0-53, weeks are from Sunday-Saturday.
-- $isoWeek ranges is 1-53, weeks are Monday-Sunday.

-- 2023-01-01 is first Sunday of the year.
SELECT * FROM bson_dollar_project('{"date": {"$date": "2023-01-01T07:30:10.5Z"}}', '{"week": {"$week": "$date"}, "isoWeek": {"$isoWeek": "$date"}}');
-- 2023-01-08 is second Sunday of the year.
SELECT * FROM bson_dollar_project('{"date": {"$date": "2023-01-08T07:30:10.5Z"}}', '{"week": {"$week": "$date"}, "isoWeek": {"$isoWeek": "$date"}}');
-- 2022-01-01 is Saturday
SELECT * FROM bson_dollar_project('{"date": {"$date": "2022-01-01T07:30:10.5Z"}}', '{"week": {"$week": {"date": "$date"}}, "isoWeek": {"$isoWeek": { "date": "$date"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2022-12-31T07:30:10.5Z"}}', '{"week": [ {"$week": "$date"} ], "isoWeek": [ {"$isoWeek": "$date"} ]}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2012-12-31T07:30:10.5Z"}}', '{"week": [ {"$week": "$date"} ], "isoWeek": [ {"$isoWeek": "$date"} ]}');
-- $week and $isoWeek: leap year
SELECT * FROM bson_dollar_project('{"date": {"$date": "2020-12-31T07:30:10.5Z"}}', '{"week": [ {"$week": "$date"} ], "isoWeek": [ {"$isoWeek": "$date"} ]}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2000-12-31T07:30:10.5Z"}}', '{"week": [ {"$week": "$date"} ], "isoWeek": [ {"$isoWeek": "$date"} ]}');

-- $dayOfYear
SELECT * FROM bson_dollar_project('{"date": {"$date": "2023-01-01T07:30:10.5Z"}}', '{"dayOfYear": {"$dayOfYear": "$date"}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2023-01-31T07:30:10.5Z"}}', '{"dayOfYear": {"$dayOfYear": [ "$date" ]}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2023-02-28T07:30:10.5Z"}}', '{"dayOfYear": {"$dayOfYear": { "date": "$date" }}}');
-- $dayOfYear: leap year should add 1 day
SELECT * FROM bson_dollar_project('{"leapDate": {"$date": "2024-03-01T07:30:10.5Z"}, "nonLeapDate": {"$date": "2023-03-01T07:30:10.5Z"}}', '{"leapDayOfYear": {"$dayOfYear": { "date": "$leapDate" }}, "nonLeapDayOfYear": {"$dayOfYear": { "date": "$nonLeapDate" }}}');
SELECT * FROM bson_dollar_project('{"leapDate": {"$date": "2024-12-31T07:30:10.5Z"}, "nonLeapDate": {"$date": "2023-12-31T07:30:10.5Z"}}', '{"leapDayOfYear": {"$dayOfYear": { "date": "$leapDate" }}, "nonLeapDayOfYear": {"$dayOfYear": { "date": "$nonLeapDate" }}}');

-- $dayOfWeek and $isoDayOfWeek both are 1-7 following $week or $isoWeek.
SELECT * FROM bson_dollar_project('{"date": {"$date": "2023-01-01T07:30:10.5Z"}}', '{"name": "Sunday", "dayOfWeek": {"$dayOfWeek": "$date"}, "isoDayOfWeek": {"$isoDayOfWeek": "$date"}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2023-01-02T07:30:10.5Z"}}', '{"name": "Monday", "dayOfWeek": {"$dayOfWeek": "$date"}, "isoDayOfWeek": {"$isoDayOfWeek": "$date"}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "1999-02-02T07:30:10.5Z"}}', '{"name": "Tuesday", "dayOfWeek": {"$dayOfWeek": "$date"}, "isoDayOfWeek": {"$isoDayOfWeek": "$date"}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2023-03-15T07:30:10.5Z"}}', '{"name": "Wednesday", "dayOfWeek": {"$dayOfWeek": "$date"}, "isoDayOfWeek": {"$isoDayOfWeek": "$date"}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-11-29T07:30:10.5Z"}}', '{"name": "Thursday", "dayOfWeek": {"$dayOfWeek": "$date"}, "isoDayOfWeek": {"$isoDayOfWeek": "$date"}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2028-05-19T07:30:10.5Z"}}', '{"name": "Friday", "dayOfWeek": {"$dayOfWeek": "$date"}, "isoDayOfWeek": {"$isoDayOfWeek": "$date"}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"name": "Saturday", "dayOfWeek": {"$dayOfWeek": "$date"}, "isoDayOfWeek": {"$isoDayOfWeek": "$date"}}');

-- $year and $isoWeekYear
SELECT * FROM bson_dollar_project('{"date": {"$date": "2023-01-01T07:30:10.5Z"}}', '{"year": {"$year": "$date"}, "isoWeekYear": {"$isoWeekYear": "$date"}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2006-01-01T07:30:10.5Z"}}', '{"year": {"$year": "$date"}, "isoWeekYear": {"$isoWeekYear": "$date"}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2006-01-02T07:30:10.5Z"}}', '{"year": {"$year": "$date"}, "isoWeekYear": {"$isoWeekYear": "$date"}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2012-12-31T07:30:10.5Z"}}', '{"year": {"$year": "$date"}, "isoWeekYear": {"$isoWeekYear": "$date"}}');

-- date part operators with nested operators
-- add 1 year to the date.
SELECT * FROM bson_dollar_project('{"date": {"$date": "2023-01-01T07:30:10.5Z"}}', '{"year": {"$year": { "$add": ["$date", {"$numberLong": "31540000000"}]}}}');

-- date part operators, null/undefined date or timezone, should return null.
SELECT * FROM bson_dollar_project('{ }', '{"year": {"$year": null}}');
SELECT * FROM bson_dollar_project('{ }', '{"month": {"$month": [ null ]}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfMonth": {"$dayOfMonth": {"date": null}}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfYear": {"$dayOfYear": {"date": "$a"}}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfWeek": {"$dayOfWeek": {"date": {"$date": "2012-12-31T07:30:10.5Z"}, "timezone": null }}}');
SELECT * FROM bson_dollar_project('{ }', '{"hour": {"$hour": [ "$a" ]}}');
SELECT * FROM bson_dollar_project('{ }', '{"minute": {"$minute": {"date": {"$date": "2012-12-31T07:30:10.5Z"}, "timezone": "$a" }}}');
SELECT * FROM bson_dollar_project('{ }', '{"second": {"$second": [ null ]}}');
SELECT * FROM bson_dollar_project('{ }', '{"millisecond": {"$millisecond": "$a"}}');
SELECT * FROM bson_dollar_project('{ }', '{"week": {"$week": {"date": {"$date": "2012-12-31T07:30:10.5Z"}, "timezone": "$a" }}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoWeekYear": {"$isoWeekYear": {"date": {"$date": "2012-12-31T07:30:10.5Z"}, "timezone": null }}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoDayOfWeek": {"$isoDayOfWeek": {"date": null}}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoWeek": {"$isoWeek": {"date": "$a"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2012-12-31T07:30:10.5Z"}}', '{"isoWeek": {"$isoWeek": {"date": "$date", "timezone": "$timezone"}}}');

-- negative tests for date part operators --
-- date part operators: date expression should be convertible to date: timestamp, object id or date time.
SELECT * FROM bson_dollar_project('{ }', '{"year": {"$year": "str"}}');
SELECT * FROM bson_dollar_project('{ }', '{"month": {"$month": true}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfMonth": {"$dayOfMonth": false}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfYear": {"$dayOfYear": false}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfWeek": {"$dayOfWeek": "str"}}');
SELECT * FROM bson_dollar_project('{ }', '{"hour": {"$hour": 1}}');
SELECT * FROM bson_dollar_project('{ }', '{"minute": {"$minute": 1}}');
SELECT * FROM bson_dollar_project('{ }', '{"second": {"$second": 1}}');
SELECT * FROM bson_dollar_project('{ }', '{"millisecond": {"$millisecond": 1}}');
SELECT * FROM bson_dollar_project('{ }', '{"week": {"$week": [[]]}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoWeekYear": {"$isoWeekYear": [[]]}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoDayOfWeek": {"$isoDayOfWeek": {"date": {}}}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoWeek": {"$isoWeek": {"date": []}}}');
SELECT * FROM bson_dollar_project('{"date": {"date": {"$date": "2012-12-31T07:30:10.5Z"} }}', '{"isoWeek": {"$isoWeek": "$date"}}');

-- date part operators: timezone argument should be string
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"year": {"$year": {"date": "$date", "timezone": true}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"month": {"$month": {"date": "$date", "timezone": false}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"dayOfMonth": {"$dayOfMonth": {"date": "$date", "timezone": []}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"dayOfYear": {"$dayOfYear": {"date": "$date", "timezone": 1}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"dayOfWeek": {"$dayOfWeek": {"date": "$date", "timezone": {}}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"hour": {"$hour": {"date": "$date", "timezone": false}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"minute": {"$minute": {"date": "$date", "timezone": [[[]]]}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"second": {"$second": {"date": "$date", "timezone": {}}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"millisecond": {"$millisecond": {"date": "$date", "timezone": 1}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"week": {"$week": {"date": "$date", "timezone": {"$date": {"$numberLong": "0"}}}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"isoWeekYear": {"$isoWeekYear": {"date": "$date", "timezone": {"$oid": "639926cee6bda3127f153bf1"}}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"isoDayOfWeek": {"$isoDayOfWeek": {"date": "$date", "timezone": true}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"isoWeek": {"$isoWeek": {"date": "$date", "timezone": true}}}');

-- date part operators: object argument.
SELECT * FROM bson_dollar_project('{ }', '{"year": {"$year": {"timezone": true}}}');
SELECT * FROM bson_dollar_project('{ }', '{"month": {"$month": {}}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfMonth": {"$dayOfMonth": {"timezone": []}}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfYear": {"$dayOfYear": {"timezone": 1}}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfWeek": {"$dayOfWeek": {"timezone": {}}}}');
SELECT * FROM bson_dollar_project('{ }', '{"hour": {"$hour": {}}}');
SELECT * FROM bson_dollar_project('{ }', '{"minute": {"$minute": {"timezone": [[[]]]}}}');
SELECT * FROM bson_dollar_project('{ }', '{"second": {"$second": {"timezone": {}}}}');
SELECT * FROM bson_dollar_project('{ }', '{"millisecond": {"$millisecond": {}}}');
SELECT * FROM bson_dollar_project('{ }', '{"week": {"$week": {"timezone": {"$date": {"$numberLong": "0"}}}}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoWeekYear": {"$isoWeekYear": {"timezone": {"$oid": "639926cee6bda3127f153bf1"}}}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoDayOfWeek": {"$isoDayOfWeek": {"timezone": true}}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoWeek": {"$isoWeek": {"timezone": true}}}');

-- date part operators: object with date and timezone arguments.
SELECT * FROM bson_dollar_project('{ }', '{"year": {"$year": {"random": true}}}');
SELECT * FROM bson_dollar_project('{ }', '{"month": {"$month": {"foo": 1}}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfMonth": {"$dayOfMonth": {"timezone": [], "date": 1, "time": 1}}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfYear": {"$dayOfYear": {"timezone": 1, "dat": 2}}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfWeek": {"$dayOfWeek": {"timezon": {}}}}');
SELECT * FROM bson_dollar_project('{ }', '{"hour": {"$hour": {"datetime": 1}}}');
SELECT * FROM bson_dollar_project('{ }', '{"minute": {"$minute": {"timeZone": [[[]]]}}}');
SELECT * FROM bson_dollar_project('{ }', '{"second": {"$second": {"Timezone": {}}}}');
SELECT * FROM bson_dollar_project('{ }', '{"millisecond": {"$millisecond": {"unknown": "1"}}}');
SELECT * FROM bson_dollar_project('{ }', '{"week": {"$week": {"location": {"$date": {"$numberLong": "0"}}}}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoWeekYear": {"$isoWeekYear": {"timezonE": {"$oid": "639926cee6bda3127f153bf1"}}}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoDayOfWeek": {"$isoDayOfWeek": {"timezone": true, "foo": 1}}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoWeek": {"$isoWeek": {"date": 1, "info": ""}}}');

-- date part operators: array argument, should be single element array.
SELECT * FROM bson_dollar_project('{ }', '{"year": {"$year": [1, 2, 3]}}');
SELECT * FROM bson_dollar_project('{ }', '{"month": {"$month": []}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfMonth": {"$dayOfMonth": [1, 2]}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfYear": {"$dayOfYear": [[], []]}}');
SELECT * FROM bson_dollar_project('{ }', '{"dayOfWeek": {"$dayOfWeek": []}}');
SELECT * FROM bson_dollar_project('{ }', '{"hour": {"$hour": [1, 2, 3, 4]}}');
SELECT * FROM bson_dollar_project('{ }', '{"minute": {"$minute": []}}');
SELECT * FROM bson_dollar_project('{ }', '{"second": {"$second": ["srr", "foo"]}}');
SELECT * FROM bson_dollar_project('{ }', '{"millisecond": {"$millisecond": [1, 2]}}');
SELECT * FROM bson_dollar_project('{ }', '{"week": {"$week": []}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoWeekYear": {"$isoWeekYear": [1, 2, 3, 4, 5]}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoDayOfWeek": {"$isoDayOfWeek": []}}');
SELECT * FROM bson_dollar_project('{ }', '{"isoWeek": {"$isoWeek": [1, []]}}');

-- date part operators: unrecognized timezones
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"year": {"$year": {"date": "$date", "timezone": ""}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"year": {"$year": {"date": "$date", "timezone": "\u0000\u0000"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"year": {"$year": {"date": "$date", "timezone": "+8"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"month": {"$month": {"date": "$date", "timezone": "+080"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"dayOfMonth": {"$dayOfMonth": {"date": "$date", "timezone": "+08:800"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"dayOfYear": {"$dayOfYear": {"date": "$date", "timezone": "-888888"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"dayOfWeek": {"$dayOfWeek": {"date": "$date", "timezone": "US/Unknown"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"hour": {"$hour": {"date": "$date", "timezone": "+"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"minute": {"$minute": {"date": "$date", "timezone": "08:09"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"second": {"$second": {"date": "$date", "timezone": "Etc/Invalid"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"millisecond": {"$millisecond": {"date": "$date", "timezone": "US/Pacifi"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"week": {"$week": {"date": "$date", "timezone": "America/LosAngeles"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"isoWeekYear": {"$isoWeekYear": {"date": "$date", "timezone": "UST"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"isoDayOfWeek": {"$isoDayOfWeek": {"date": "$date", "timezone": "+080\u00001"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"isoWeek": {"$isoWeek": {"date": "$date", "timezone": "+080\u0000"}}}');

-- $dateToParts no timezone
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "1990-01-01T23:13:00.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date", "iso8601": true }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "0006-12-31T15:13:25.003Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "0006-12-31T15:13:25.003Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date", "iso8601": false }}}');

-- $dateToParts with timezone
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date", "timezone": "UTC" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date", "timezone": "Europe/London" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date", "timezone": "America/New_York" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date", "timezone": "+01" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date", "timezone": "-01" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date", "timezone": "-0001" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}, "tz": "America/New_York"}', '{"dateParts": {"$dateToParts": { "date": "$date", "timezone": "$tz", "iso8601": {"$eq": ["$tz", "America/New_York"]} }}}');
-- oid == 2017-06-19T15:13:25.713Z UTC.
SELECT * FROM bson_dollar_project('{"_id": {"$oid": "58c7cba47bbadf523cf2c313"}}', '{"dateParts": {"$dateToParts": { "date": "$_id", "timezone": "Europe/London" }}}');

-- $dateToParts should return null
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date", "timezone": "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date", "timezone": null }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date", "timezone": "UTC", "iso8601": "$iso" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date", "timezone": "UTC", "iso8601": null }}}');
SELECT * FROM bson_dollar_project('{"_id": 1 }', '{"dateParts": {"$dateToParts": { "date": "$date", "timezone": "UTC", "iso8601": true }}}');
SELECT * FROM bson_dollar_project('{"_id": 1 }', '{"dateParts": {"$dateToParts": { "date": null, "timezone": "UTC", "iso8601": true }}}');

-- $dateToParts 'date' argument is required
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "timezone": "$tz" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "timezone": "$tz", "iso8601": true }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "timezone": "Unknown", "iso8601": true }}}');

-- $dateToParts 'iso8601' should be a bool
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date", "iso8601": {"$add": [1, 1]} }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "$date", "iso8601": "str" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": {"$add": [1, 1]}, "iso8601": "str" }}}');

-- $dateToParts 'date' must be a date, timestamp or oid
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date":  {"$add": [1, 1]}, "iso8601": true }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": "str" }}}');

-- $dateToParts 'timezone' must be a string
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": {"$add": [1, 1]}, "iso8601": "str", "timezone": 1 }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateParts": {"$dateToParts": { "date": {"$add": [1, 1]}, "iso8601": "str", "timezone": {"$add": [1]} }}}');

-- $dateToParts 'timezone' is unknown
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"dateToParts": {"$dateToParts": {"date": "$date", "timezone": ""}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"dateToParts": {"$dateToParts": {"date": "$date", "timezone": "\u0000\u0000"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"dateToParts": {"$dateToParts": {"date": "$date", "timezone": "+8"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"dateToParts": {"$dateToParts": {"date": "$date", "timezone": "+080"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"dateToParts": {"$dateToParts": {"date": "$date", "timezone": "+08:800"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"dateToParts": {"$dateToParts": {"date": "$date", "timezone": "-888888"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"dateToParts": {"$dateToParts": {"date": "$date", "timezone": "Ut"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2035-06-23T07:30:10.5Z"}}', '{"dateToParts": {"$dateToParts": {"date": "$date", "timezone": "Unknown"}}}');

-- $dateToString no timezone default format
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateString": {"$dateToString": { "date": "$date" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "0006-06-01T00:01:02.003Z"}}', '{"dateString": {"$dateToString": { "date": "$date" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "0906-06-01T08:01:02.023Z"}}', '{"dateString": {"$dateToString": { "date": "$date" }}}');

-- $dateToString with timezone default format
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "timezone": "UTC" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "timezone": "America/Los_Angeles" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-01-19T15:13:25.713Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "timezone": "America/Los_Angeles" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "timezone": "Europe/London" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-01-19T15:13:25.713Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "timezone": "Europe/London" }}}');

-- $dateToString random formats
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%d%d***%d***%d**%d*%d" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "0006-06-01T00:01:02.003Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%%%%%%%%%%%%%%%%%%---%Y---%%%%%%%%%%%%" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "0906-06-01T08:01:02.023Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "This is a custom format:\u0000....%Y-%d-%m....\u0000"}}}');

-- $dateToString timezone specifiers
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-06-19T15:13:25.713Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "UTC offset: %z (%Z minutes)", "timezone": "America/Los_Angeles" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-01-19T15:13:25.713Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "UTC offset: %z (%Z minutes)","timezone": "America/Los_Angeles" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-01-19T15:13:25.713Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "UTC offset: %z (%Z minutes)","timezone": "Africa/Asmera" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-01-19T15:13:25.713Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "UTC offset: %z (%Z minutes)","timezone": "+08" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-01-19T15:13:25.713Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "UTC offset: %z (%Z minutes)","timezone": "+0059" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-01-19T15:13:25.713Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "UTC offset: %z (%Z minutes)","timezone": "+5959" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-01-19T15:13:25.713Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "UTC offset: %z (%Z minutes)","timezone": "-00:01" }}}');

-- $dateToString multiple timezones
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%Y-%m-%d %H:%M:%S %z (%Z minutes)", "timezone": "UTC" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%Y-%m-%d %H:%M:%S %z (%Z minutes)", "timezone": "Europe/London" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%Y-%m-%d %H:%M:%S %z (%Z minutes)", "timezone": "America/New_York" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%Y-%m-%d %H:%M:%S %z (%Z minutes)", "timezone": "Australia/Eucla" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%Y-%m-%d %H:%M:%S %z (%Z minutes)", "timezone": "Asia/Katmandu" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%Y-%m-%d %H:%M:%S %z (%Z minutes)", "timezone": "Europe/Amsterdam" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "1900-07-10T11:41:22.418Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%Y-%m-%d %H:%M:%S %z (%Z minutes)", "timezone": "America/Caracas" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-01-04T15:08:51.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%Y-%m-%d %H:%M:%S %z (%Z minutes)", "timezone": "America/New_York" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T15:09:12.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%Y-%m-%d %H:%M:%S %z (%Z minutes)", "timezone": "America/New_York" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-12-04T15:09:14.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%Y-%m-%d %H:%M:%S %z (%Z minutes)", "timezone": "America/New_York" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-12-04T15:09:14.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%Y-%m-%d %H:%M:%S %z (%Z minutes)" }}}');

-- $dateToString natural vs iso dates
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-01-01T15:08:51.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "Natural: %Y-W%w-%U-d%j, ISO: %G-W%u-%V-d%j" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T15:09:12.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "Natural: %Y-W%w-%U-d%j, ISO: %G-W%u-%V-d%j" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-12-04T15:09:14.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "Natural: %Y-W%w-%U-d%j, ISO: %G-W%u-%V-d%j" }}}');

-- $dateToString long format strings > 256 bytes
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-01-01T15:08:51.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "The order: RIMSLABOZXERCQTXYYSJFUOPCAUWTDPXHEMCUUTUWZZSPLMXNWSIYDZNMIX was placed on: %Y-%m-%d %H:%M:%S" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2038-01-01T15:08:51.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "The order: RIMSLABOZXERCQTXYYSJFUOPCAUWTDPXHEMCUUTUWZZSPLMXNWSIYDZ was placed on: %Y-%m-%d %H:%M:%S" }}}');

-- $dateToString returns null if no onNull is specified and any arg expression is null or undefined
SELECT * FROM bson_dollar_project('{"date": null}', '{"dateString": {"$dateToString": { "date": "$date", "format": "Natural: %Y-W%w-%U, ISO: %G-W%u-%V" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-12-04T15:09:14.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "timezone": "$tz", "format": "Natural: %Y-W%w-%U, ISO: %G-W%u-%V" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-12-04T15:09:14.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "timezone": null, "format": "Natural: %Y-W%w-%U, ISO: %G-W%u-%V" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-12-04T15:09:14.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "timezone": "America/New_York", "format": "$format" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-12-04T15:09:14.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "timezone": "America/New_York", "format": null }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-12-04T15:09:14.911Z"}}', '{"dateString": {"$dateToString": { "date": null, "timezone": "America/New_York"}}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-12-04T15:09:14.911Z"}}', '{"dateString": {"$dateToString": { "date": {"$literal": null}, "timezone": "America/New_York"}}}');

-- $dateToString onNull expression is only returned and honored when 'date' argument is null
SELECT * FROM bson_dollar_project('{"date": null}', '{"dateString": {"$dateToString": { "date": "$date", "format": "Natural: %Y-W%w-%U, ISO: %G-W%u-%V", "onNull": "date was null or undefined" }}}');
SELECT * FROM bson_dollar_project('{"date": null}', '{"dateString": {"$dateToString": { "date": "$date", "format": "Natural: %Y-W%w-%U, ISO: %G-W%u-%V", "onNull": "$date" }}}');
SELECT * FROM bson_dollar_project('{"date": null}', '{"dateString": {"$dateToString": { "date": "$date", "format": "Natural: %Y-W%w-%U, ISO: %G-W%u-%V", "onNull": {"$date": "2017-12-04T15:09:14.911Z"} }}}');
SELECT * FROM bson_dollar_project('{"date": null}', '{"dateString": {"$dateToString": { "date": "$newYorkDate", "format": "Natural: %Y-W%w-%U, ISO: %G-W%u-%V", "onNull": "date was null or undefined" }}}');
SELECT * FROM bson_dollar_project('{"date": null}', '{"dateString": {"$dateToString": { "date": "$date", "format": "Natural: %Y-W%w-%U, ISO: %G-W%u-%V", "timezone": "$tz", "onNull": "date was null or undefined" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-12-04T15:09:14.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "$format", "onNull": "date was null or undefined" }}}');

-- $dateToString when date is null and onNull expression points to a non existent path in the document, no result is returned
SELECT * FROM bson_dollar_project('{"date": null}', '{"dateString": {"$dateToString": { "date": "$date", "format": "Natural: %Y-W%w-%U, ISO: %G-W%u-%V", "onNull": "$a" }}}');
SELECT * FROM bson_dollar_project('{"date": null}', '{"dateString": {"$dateToString": { "date": "$date", "format": "Natural: %Y-W%w-%U, ISO: %G-W%u-%V", "onNull": "$b" }}}');
SELECT * FROM bson_dollar_project('{"date": null}', '{"dateString": {"$dateToString": { "date": "$date", "format": "Natural: %Y-W%w-%U, ISO: %G-W%u-%V", "onNull": "$c" }}}');

-- $dateToString 'date' is required
SELECT * FROM bson_dollar_project('{"date": null}', '{"dateString": {"$dateToString": { "format": "Natural: %Y-W%w-%U, ISO: %G-W%u-%V", "onNull": "$a" }}}');

-- $dateToString unknown field
SELECT * FROM bson_dollar_project('{"date": null}', '{"dateString": {"$dateToString": { "date": "$date", "format": "Natural: %Y-W%w-%U, ISO: %G-W%u-%V", "onNull": "$a", "customField": "a" }}}');

-- $dateToString an object is required as an argument
SELECT * FROM bson_dollar_project('{"arg": {"date": null, "format": "%Y", "timezone": "UTC"}}', '{"dateString": {"$dateToString": "$arg"}}');
SELECT * FROM bson_dollar_project('{"arg": {"date": null, "format": "%Y", "timezone": "UTC"}}', '{"dateString": {"$dateToString": null}}');
SELECT * FROM bson_dollar_project('{"arg": {"date": null, "format": "%Y", "timezone": "UTC"}}', '{"dateString": {"$dateToString": []}}');

-- $dateToString invalid format modifier
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%Y-%m-%d %i %H:%M:%S %z (%Z minutes)", "timezone": "UTC" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%i %Y-%m-%d %H:%M:%S %z (%Z minutes)", "timezone": "UTC" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%Y-%m-%d %H:%M:%S %z (%Z minutes) %i", "timezone": "UTC" }}}');

-- $dateToString unmatched % at end of format string
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": "%Y-%m-%d %H:%M:%S %z (%Z minutes)%%%", "timezone": "UTC" }}}');

-- $dateToString 'format' argument must be a string
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": true, "timezone": "UTC" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "format": 1, "timezone": "UTC" }}}');

-- $dateToString 'date' must be an oid, timestamp or date
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "", "timezone": "UTC" }}}');

-- $dateToString 'timezone' must be string and a valid timezone
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "timezone": true }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "timezone": "Unknown" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "timezone": "+8" }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "2017-07-04T14:56:42.911Z"}}', '{"dateString": {"$dateToString": { "date": "$date", "timezone": "+80000000" }}}');

-- $dateToString date component outside of supported range
SELECT * FROM bson_dollar_project('{"date": {"$date": "9999-12-31T11:59:59.911Z"}}', '{"dateString": {"$dateToString": { "date": {"$add": ["$date", {"$numberLong": "86400000"}]} }}}');
SELECT * FROM bson_dollar_project('{"date": {"$date": "9999-12-31T11:59:59.911Z"}}', '{"dateString": {"$dateToString": { "date": {"$add": ["$date", {"$numberLong": "86400000"}]}, "format": "%Y" }}}');


-- $dateFromParts null cases
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": null }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"$undefined":true} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": null} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2029, "month": null} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2029, "month": 5, "day":null} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2029, "month": 5, "day": 10, "hour":null} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2029, "month": 5, "day": 10, "hour":2 , "minute":null} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2029, "month": 5, "day": 10, "hour":2 , "minute":20, "second":null} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2029, "month": 5, "day": 10, "hour":2 , "minute":20, "second":5, "millisecond":null} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2029, "month": 5, "day": 10, "hour":2 , "minute":20, "second":5, "millisecond":200, "timezone":null} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear" : null} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear" : 2029, "isoWeek": null} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear" : 2029, "isoWeek": 5, "isoDayOfWeek":null} }}');

-- $dateFromParts undefined cases
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": {"$undefined":true}} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2029, "month": {"$undefined":true}} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2029, "month": 5, "day":{"$undefined":true}} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2029, "month": 5, "day": 10, "hour":{"$undefined":true}} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2029, "month": 5, "day": 10, "hour":2 , "minute":{"$undefined":true}} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2029, "month": 5, "day": 10, "hour":2 , "minute":20, "second":{"$undefined":true}} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2029, "month": 5, "day": 10, "hour":2 , "minute":20, "second":5, "millisecond":{"$undefined":true}} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2029, "month": 5, "day": 10, "hour":2 , "minute":20, "second":5, "millisecond":200, "timezone":{"$undefined":true}} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear" : {"$undefined":true}} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear" : 2029, "isoWeek": {"$undefined":true}} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear" : 2029, "isoWeek": 5, "isoDayOfWeek":{"$undefined":true}} }}');

-- $dateFromParts non-iso format cases
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 2029} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2000, "month": 5, "day":5} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2020, "month": 5, "day": 10, "hour":2 , "minute":20, "second":5, "millisecond":450} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 1970, "month": 2, "day": 10, "hour":23 , "minute":59, "second":50, "millisecond":450} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2000, "month": 2, "day": 29, "hour":23 , "minute":59, "second":50, "millisecond":450} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 1, "month": 2, "day": 28, "hour":23 , "minute":59, "second":50, "millisecond":450} }}');

-- $dateFromParts non-iso format cases with timezone
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2020, "month": 5, "day": 10, "hour":2 , "minute":20, "second":5, "millisecond":450, "timezone": "UTC"} }}'); 
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2020, "month": 5, "day": 10, "hour":2 , "minute":20, "second":5, "millisecond":450, "timezone": "America/Denver"} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2020, "month": 5, "day": 10, "hour":2 , "minute":20, "second":5, "millisecond":450, "timezone": "Asia/Kolkata"} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2020, "month": 5, "day": 10, "hour":2 , "minute":20, "second":5, "millisecond":450, "timezone": "+04:30"} }}'); 
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2020, "month": 5, "day": 10, "hour":2 , "minute":20, "second":5, "millisecond":450, "timezone": "+0430"} }}'); 

-- $dateFromParts iso format
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2029} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2029, "isoWeek":20, "isoDayOfWeek": 2} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2029, "isoWeek":20, "isoDayOfWeek": 2 ,"hour":20, "minute":45, "second":12} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2023, "isoWeek":12, "isoDayOfWeek": 2} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2024, "isoWeek":50, "isoDayOfWeek": 2 ,"hour":18, "minute":45, "second":12} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 1, "isoWeek":12, "isoDayOfWeek": 2 ,"hour":18, "minute":45, "second":12} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 1, "isoWeek":12, "isoDayOfWeek": 2 ,"hour":18, "minute":45, "second":12, "timezone": "+04:30"} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 1, "isoWeek":12, "isoDayOfWeek": 2 ,"hour":18, "minute":45, "second":12, "timezone": "+0430"} }}');

-- $dateFromParts iso cases with timezone
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2029, "isoWeek":20, "isoDayOfWeek": 2 ,"hour":20, "minute":45, "second":12, "timezone": "America/Denver"} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2023, "isoWeek":12, "isoDayOfWeek": 2, "timezone": "America/Los_Angeles"} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2024, "isoWeek":50, "isoDayOfWeek": 2 ,"hour":20, "minute":45, "second":12 , "timezone": "Asia/Kolkata"} }}');

-- $dateFromParts cases with carrying when range is not valid for iso format
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2017, "isoWeek":0, "isoDayOfWeek": 2 ,"hour":18, "minute":45, "second":12} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2000, "isoWeek":0, "isoDayOfWeek": 5 ,"hour":18, "minute":45, "second":12} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2000, "isoWeek":0, "isoDayOfWeek": 15 ,"hour":28, "minute":450, "second":124 , "millisecond":876} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 1, "isoWeek":0, "isoDayOfWeek": 15 ,"hour":28, "minute":450, "second":124 , "millisecond":876} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2011, "isoWeek":-24, "isoDayOfWeek": -15 ,"hour":28, "minute":450, "second":124 , "millisecond":876} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2011, "isoWeek":-24, "isoDayOfWeek": -15 ,"hour":-28, "minute":-450, "second":-124 , "millisecond":876} }}');

-- $dateFromParts cases with carrying when range is not valid for non-iso format
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2020, "month": 15, "day": 100, "hour":23 , "minute":210, "second":1115, "millisecond":450} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 2020, "month": -15, "day": 1200, "hour":213 , "minute":2110, "second":-1115, "millisecond":-450} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 1, "month": -15, "day": 1200, "hour":213 , "minute":2110, "second":-1115, "millisecond":-450} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year" : 9999, "month": -15, "day": 1200, "hour":213 , "minute":2110, "second":-1115, "millisecond":-450} }}');

-- $dateFromParts cases negative cases
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": 1 }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": "abc" }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year":2017, "isoWeekYear": 2012} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": {"$numberDecimal": "2014.5"} } }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": 1 }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": "abc" }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year":2017, "isoWeekYear": 2012} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year":2017.5} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year":"2017"} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 2017, "abcd":1} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2012.12} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": "2012"} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2012, "timezone": "hello"} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 2012, "timezone": 1} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 0} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 11110} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 0} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 11110} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 2010, "month": 32769, "day": 100, "second": 50, "minute":100} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 2010, "month": 12, "day": 32769, "second": 50, "minute":100} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 2010, "month": 12, "day": 1, "second": 32769, "minute":100} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 2010, "month": 12, "day": 1, "hour": 32769, "minute":100} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 2010, "month": 12, "day": 1, "hour": -24, "minute":32769} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 2010, "month": -32769, "day": 100, "second": 50, "minute":100} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 2010, "month": 12, "day": -32769, "second": 50, "minute":100} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 2010, "month": 12, "day": 1, "second": -32769, "minute":100} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 2010, "month": 12, "day": 1, "hour": -32769, "minute":100} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 2010, "month": 12, "day": 1, "hour": -24, "minute":-32769} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 2010, "millisecond": {"$numberDecimal": "-18999999999999999888888888"}} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2012, "month":12} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeekYear": 2012, "day":12} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 1209, "isoWeek": 12} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"year": 1209, "isoDayOfWeek": 12} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"month": 1209, "isoDayOfWeek": 12} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"month": 1209} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateFromParts": {"isoWeek": 1209} }}');


-- $dateFromParts document based cases
select * from bson_dollar_project('{"year" : 2011, "month": 2, "day": 28, "hour":23 , "minute":59, "second":50, "millisecond":450}', '{"result": {"$dateFromParts": {"year" : "$year", "month": "$month", "day": "$day", "hour":"$hour" , "minute":"$minute", "second":"$second", "millisecond":"$millisecond"} }}');
select * from bson_dollar_project('{"year" : 2011, "month": 2, "day": 28, "hour":23 , "minute":59, "second":50, "millisecond":450}', '{"result": {"$dateFromParts": {"year" : "$year", "month": "$month"} }}');
select * from bson_dollar_project('{"year" : 2011, "month": 2, "day": 28, "hour":23 , "minute":59, "second":50, "millisecond":450, "timezone": "America/Los_Angeles"}', '{"result": {"$dateFromParts": {"year" : "$year", "month": "$month" , "timezone": "$timezone"} }}');
select * from bson_dollar_project(' {"isoWeekYear": 2024, "isoWeek":50, "isoDayOfWeek": 2 ,"hour":18, "minute":45, "second":12}', '{"result": {"$dateFromParts":  {"isoWeekYear": "$isoWeekYear", "isoWeek": "$isoWeek", "isoDayOfWeek": "$isoDayOfWeek" ,"hour":"$hour", "minute":"$minute", "second":"$second"} }}');
select * from bson_dollar_project(' {"isoWeekYear": 2024, "isoWeek":50, "isoDayOfWeek": 2 ,"hour":18, "minute":45, "second":12}', '{"result": {"$dateFromParts":  {"isoWeekYear": "$isoWeekYear", "isoWeek": "$isoWeek", "isoDayOfWeek": "$isoDayOfWeek"} }}');
select * from bson_dollar_project(' {"isoWeekYear": 2024, "isoWeek":50, "isoDayOfWeek": 2 ,"hour":18, "minute":45, "second":12, "timezone" : "America/New_York"}', '{"result": {"$dateFromParts":  {"isoWeekYear": "$isoWeekYear", "isoWeek": "$isoWeek", "isoDayOfWeek": "$isoDayOfWeek", "timezone": "$timezone"} }}');


--$dateTrunc null cases
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": null }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"$undefined": true} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "second", "binSize": null  }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "second", "binSize": {"$undefined": true}  }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "second", "timezone": null , "binSize": 5  }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "second", "timezone": {"$undefined": true} , "binSize":5 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "timezone": "+04:45", "unit": null , "binSize": 5  }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "timezone": "-0430", "unit": {"$undefined": true} , "binSize":5 }  }}');

-- $dateTrunc case when startOfWeek is null
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "week", "startOfWeek": null , "binSize": 2 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "week", "startOfWeek": {"$undefined": true}, "binSize": 2  }  }}');

-- $dateTrunc case when startOfWeek null but unit is not week
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "second", "startOfWeek": null , "binSize": 2 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "second", "startOfWeek": {"$undefined": true} , "binSize": 2 }  }}');

-- $dateTrunc cases with unit millisecond
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "millisecond", "binSize": 2 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "millisecond" , "binSize": 200000 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "millisecond", "binSize": 2, "timezone": "America/Denver" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "millisecond" , "binSize": 200000, "timezone": "Asia/Kolkata" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "millisecond" , "binSize": 4500 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "millisecond" , "binSize": 450000000 }  }}');

-- $dateTrunc cases with unit second
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "second", "binSize": 2 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "second" , "binSize": 200000 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "second", "binSize": 2, "timezone": "America/Denver" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "second" , "binSize": 200000, "timezone": "Asia/Kolkata" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "second" , "binSize": 4500 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "second" , "binSize": 450000000 }  }}');

-- $dateTrunc cases with unit minute
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "minute", "binSize": 2 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "minute" , "binSize": 200000 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "minute", "binSize": 2, "timezone": "America/Denver" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "minute" , "binSize": 200000, "timezone": "Asia/Kolkata" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "minute" , "binSize": 4500 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "minute" , "binSize": 450000000 }  }}');

-- $dateTrunc cases with unit hour
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "hour", "binSize": 2 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "hour" , "binSize": 200000 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "hour", "binSize": 2, "timezone": "America/Los_Angeles" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "hour" , "binSize": 200000, "timezone": "America/New_York" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "hour" , "binSize": 200000, "timezone": "America/Denver" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "hour" , "binSize": 4500 }  }}');

/* This test case has overflow, postgres added this check in newer minor versions.
 * Todo: add this test back and check overflow after we bump up the postgres minor version (>=15.7 and >=16.3).
 */
-- select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "hour" , "binSize": 450000000 }  }}');

-- $dateTrunc cases with unit day
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "day", "binSize": 2 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "day" , "binSize": 200000 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "day", "binSize": 2, "timezone": "America/Denver" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "day" , "binSize": 200000, "timezone": "Asia/Kolkata" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "day" , "binSize": 4500 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "day" , "binSize": 450000000 }  }}');


-- $dateTrunc cases with unit week
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "week", "binSize": 2 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "week" , "binSize": 200000 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "week", "binSize": 2, "timezone": "America/Denver" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "week" , "binSize": 200000, "timezone": "Asia/Kolkata" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "week" , "binSize": 4500 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "week" , "binSize": 450000000 }  }}');

-- $dateTrunc cases with unit month
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "month", "binSize": 2 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "month" , "binSize": 200000 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "month", "binSize": 2, "timezone": "America/Denver" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "month" , "binSize": 200000, "timezone": "Asia/Kolkata" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "month" , "binSize": 4500 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "month" , "binSize": 450000000 }  }}');

-- $dateTrunc cases with unit year
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "year", "binSize": 2 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}, "unit": "year" , "binSize": 200000 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "year", "binSize": 2, "timezone": "America/Denver" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "951850555381"}}, "unit": "year" , "binSize": 200000, "timezone": "Asia/Kolkata" }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "year" , "binSize": 4500 }  }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "7498555381"}}, "unit": "year" , "binSize": 450000000 }  }}');

-- $dateTrunc invalid cases
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": "A" }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"abc": 1} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}}  } }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}} , "unit": 1 } }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}} , "unit": "456" } }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}} , "unit": "456" } }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}} , "unit": "year", "binSize": "1" } }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}} , "unit": "year", "binSize": 1.5 } }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}} , "unit": "year", "binSize": 1 , "timezone": 123} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": {"$date" : {"$numberLong": "1707123355381"}} , "unit": "week", "binSize": 1 , "startOfWeek": "Menday"} }}');
select * from bson_dollar_project('{}', '{"result": {"$dateTrunc": {"date": "A" , "unit": "456" } }}');

-- $dateAdd null based cases
select * from bson_dollar_project('{}','{"result": {"$dateAdd": null } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"$undefined":true} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {} } }');

-- $dateAdd cases when input parts are null or undefined
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": null , "unit":"month" , "amount":30000000000, "timezone": "+04:00" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":null , "amount":30000000000, "timezone": "+04:00" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"second" , "amount":null, "timezone": "+04:00" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"second" , "amount":1, "timezone": null } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$undefined":true} , "unit":"month" , "amount":30000000000, "timezone": "+04:00" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":{"$undefined":true} , "amount":30000000000, "timezone": "+04:00" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"second" , "amount":{"$undefined":true}, "timezone": "+04:00" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"second" , "amount":1, "timezone": {"$undefined":true} } } }');

-- $dateAdd normal cases
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"second" , "amount":300 } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"month" , "amount":-30 } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"year" , "amount":3 } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"quarter" , "amount":45 } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"day" , "amount":145 } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"week" , "amount":-14 } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"hour" , "amount":4 } } }');

-- $dateAdd cases with timezone handling
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"month" , "amount":2,  "timezone": "America/New_York" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"hour" , "amount":12,  "timezone": "America/New_York" } } }');

-- dst offset not applied for hours
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1603563000000"} } , "unit":"hour" , "amount":24,  "timezone": "Europe/Paris" } } }');
-- dst offset applied
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1603563000000"} } , "unit":"day" , "amount":1,  "timezone": "Europe/Paris" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1603563000000"} } , "unit":"day" , "amount":1,  "timezone": "+04:00" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1603563000000"} } , "unit":"second" , "amount":1,  "timezone": "-05:30" } } }');
-- dst offset applied
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"day" , "amount":5,  "timezone": "America/New_York" } } }');

-- $dateAdd cases with input validation of ranges which are calculated manually. With these inputs we should error that it's out of range. This is a pre-addition validation
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"year" , "amount": 584942419} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"quarter" , "amount": 1754827252} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"month" , "amount": 7019309005} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"week" , "amount": 30500568906} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"week" , "amount": 30500568906} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"day" , "amount": 213503982336} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"hour" , "amount": 5124095576041} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"minute" , "amount": 307445734562401} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"second" , "amount": 18446744073744001} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"second" , "amount": 1, "hex": "a"} } }');

-- $dateAdd cases with error cases
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"year" , "amount": 56.55} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"years" , "amount": 56} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"year" , "amount": 56, "field":5} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"year" , "amount": 56, "field":5} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} }  , "amount": 56} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"unit":"day"  , "amount": 56} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"unit":"day"  , "amount": 56} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": "abcd"  , "amount": 56, "unit": 5} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1612062000000"} }  , "amount": 56, "unit": 5, "timezone": "abcd"} } }');

-- $dateAdd cases with overflow
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1603563000000"} } , "unit":"year" , "amount": 584942416} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1603563000000"} } , "unit":"month" , "amount": 584942416} } }');
select * from bson_dollar_project('{}','{"result": {"$dateAdd": {"startDate": {"$date": {"$numberLong": "1603563000000"} } , "unit":"hour" , "amount": 58411942416} } }');

-- $dateAdd cases with document
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"second" , "amount":300 }','{"result": {"$dateAdd": {"startDate": "$startDate", "amount": "$amount", "unit":"$unit"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"month" , "amount":3 }','{"result": {"$dateAdd": {"startDate": "$startDate", "amount": "$amount", "unit":"$unit"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"year" , "amount":10 }','{"result": {"$dateAdd": {"startDate": "$startDate", "amount": "$amount", "unit":"$unit"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"day" , "amount":60 }','{"result": {"$dateAdd": {"startDate": "$startDate", "amount": "$amount", "unit":"$unit"} } }');

-- $dateSubtract null based cases
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": null } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"$undefined":true} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {} } }');

-- $dateSubtract cases when input parts are null or undefined
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": null , "unit":"month" , "amount":30000000000, "timezone": "+04:00" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":null , "amount":30000000000, "timezone": "+04:00" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"second" , "amount":null, "timezone": "+04:00" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"second" , "amount":1, "timezone": null } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$undefined":true} , "unit":"month" , "amount":30000000000, "timezone": "+04:00" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":{"$undefined":true} , "amount":30000000000, "timezone": "+04:00" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"second" , "amount":{"$undefined":true}, "timezone": "+04:00" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"second" , "amount":1, "timezone": {"$undefined":true} } } }');

-- $dateSubtract normal cases
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"second" , "amount":300 } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"month" , "amount":-30 } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"year" , "amount":3 } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1709205288000"} } , "unit":"year" , "amount":1 } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"quarter" , "amount":45 } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"day" , "amount":145 } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"week" , "amount":-14 } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"hour" , "amount":4 } } }');

-- $dateSubtract cases with timezone handling
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"month" , "amount":2,  "timezone": "America/New_York" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"hour" , "amount":12,  "timezone": "America/New_York" } } }');
-- dst offset not applied for hours
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1603563000000"} } , "unit":"hour" , "amount":24,  "timezone": "Europe/Paris" } } }');
-- dst offset applied
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1603563000000"} } , "unit":"day" , "amount":1,  "timezone": "Europe/Paris" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1603563000000"} } , "unit":"day" , "amount":1,  "timezone": "+04:00" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1603563000000"} } , "unit":"second" , "amount":1,  "timezone": "-05:30" } } }');
-- dst offset applied
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"day" , "amount":5,  "timezone": "America/New_York" } } }');

-- $dateSubtract cases with input validation of ranges which are calculated manually. With these inputs we should error that it's out of range. This is a pre-subtraction validation
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"year" , "amount": 584942419} } }'); 
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"quarter" , "amount": 1754827252} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"month" , "amount": 7019309005} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"week" , "amount": 30500568906} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"week" , "amount": 30500568906} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"day" , "amount": 213503982336} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"hour" , "amount": 5124095576041} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"minute" , "amount": 307445734562401} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"second" , "amount": 18446744073744001} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"second" , "amount": 1, "hex": "a"} } }');

-- $dateSubtract cases with error cases
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"year" , "amount": 56.55} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"years" , "amount": 56} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"year" , "amount": 56, "field":5} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} } , "unit":"year" , "amount": 56, "field":5} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} }  , "amount": 56} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"unit":"day"  , "amount": 56} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"unit":"day"  , "amount": 56} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": "abcd"  , "amount": 56, "unit": 5} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1612062000000"} }  , "amount": 56, "unit": 5, "timezone": "abcd"} } }');

-- $dateSubtract cases with overflow
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1603563000000"} } , "unit":"year" , "amount": 584942416} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1603563000000"} } , "unit":"month" , "amount": 584942416} } }');
select * from bson_dollar_project('{}','{"result": {"$dateSubtract": {"startDate": {"$date": {"$numberLong": "1603563000000"} } , "unit":"hour" , "amount": 58411942416} } }');

-- $dateSubtract cases with document
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"second" , "amount":300 }','{"result": {"$dateSubtract": {"startDate": "$startDate", "amount": "$amount", "unit":"$unit"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"month" , "amount":3 }','{"result": {"$dateSubtract": {"startDate": "$startDate", "amount": "$amount", "unit":"$unit"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"year" , "amount":10 }','{"result": {"$dateSubtract": {"startDate": "$startDate", "amount": "$amount", "unit":"$unit"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1615647600000"} } , "unit":"day" , "amount":60 }','{"result": {"$dateSubtract": {"startDate": "$startDate", "amount": "$amount", "unit":"$unit"} } }');

-- $dateDiff null cases
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": null , "unit":"minute" , "endDate": {"$date": {"$numberLong": "1678924740000"} } } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740000"} } , "unit":"minute" , "endDate": null } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": null  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "week" , "timezone": null  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "week" , "timezone": "-05:00", "startOfWeek" : null  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$undefined":true} , "unit":"minute" , "endDate": {"$date": {"$numberLong": "1678924740000"} } } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740000"} } , "unit":"minute" , "endDate": {"$undefined":true} } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": {"$undefined":true}  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "week" , "timezone": {"$undefined":true}  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "week" , "timezone": "-05:00", "startOfWeek" : {"$undefined":true}  } } }');

-- $dateDiff null cases with document
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "millisecond", "timezone": "America/New_York"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezones"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "millisecond", "timezone": "America/New_York"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unist", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "millisecond", "timezone": "America/New_York"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDsate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "millisecond", "timezone": "America/New_York"  }','{"result": {"$dateDiff": {"startDate": "$sstartDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');

-- startOfWeek is ignored when unit is not week
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "hour" , "timezone": "-05:00", "startOfWeek" : null  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "hour" , "timezone": "-05:00", "startOfWeek" : 1  } } }');

-- $dateDiff unit milliseconds
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "millisecond"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "millisecond"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740500"} } , "endDate": {"$date": {"$numberLong": "4740000"} }, "unit": "millisecond"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "millisecond", "timezone": "UTC"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "millisecond", "timezone": "America/New_York"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "millisecond", "timezone": "America/New_York"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "millisecond", "timezone": "+05:00"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');

-- $dateDiff unit seconds
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "second"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "second"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740500"} } , "endDate": {"$date": {"$numberLong": "4740000"} }, "unit": "second"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "second", "timezone": "UTC"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "second", "timezone": "America/New_York"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "second", "timezone": "America/New_York"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "second", "timezone": "+05:00"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');

-- $dateDiff unit minute
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "minute"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "minute"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740500"} } , "endDate": {"$date": {"$numberLong": "4740000"} }, "unit": "minute"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "minute", "timezone": "UTC"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "minute", "timezone": "America/New_York"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "minute", "timezone": "America/New_York"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "minute", "timezone": "+05:00"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678406400000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "minute", "timezone": "America/New_York"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678406400000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "minute"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit"} } }');

-- $dateDiff unit hour
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "hour"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "hour"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740500"} } , "endDate": {"$date": {"$numberLong": "4740000"} }, "unit": "hour"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "hour", "timezone": "UTC"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "hour", "timezone": "America/New_York"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "hour", "timezone": "America/New_York"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "hour", "timezone": "+05:00"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678406400000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "hour", "timezone": "America/New_York"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678406400000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "hour"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1604255016000"} } , "endDate": {"$date": {"$numberLong": "1604275200000"} }, "unit": "hour"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1604255016000"} } , "endDate": {"$date": {"$numberLong": "1604275200000"} }, "unit": "hour", "timezone": "America/New_York"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"} } }');

-- $dateDiff unit day
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "day"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "day"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740500"} } , "endDate": {"$date": {"$numberLong": "4740000"} }, "unit": "day"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "day", "timezone": "UTC"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "day", "timezone": "America/New_York"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "day", "timezone": "America/New_York"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "day", "timezone": "+05:00"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678406400000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "day", "timezone": "America/New_York"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678406400000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "day"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1604255016000"} } , "endDate": {"$date": {"$numberLong": "1604275200000"} }, "unit": "day"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1604255016000"} } , "endDate": {"$date": {"$numberLong": "1604275200000"} }, "unit": "day", "timezone": "America/New_York"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"} } }');
-- days in 2024 year
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1704067200000"} } , "endDate": {"$date": {"$numberLong": "1735603200000"} }, "unit": "day"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1704067200000"} } , "endDate": {"$date": {"$numberLong": "1735603200000"} }, "unit": "day", "timezone": "America/Denver"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"} } }');

-- $dateDiff unit week
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "week"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "week"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678924740500"} } , "endDate": {"$date": {"$numberLong": "4740000"} }, "unit": "week"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "week", "timezone": "UTC"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "week", "timezone": "America/New_York"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678920740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "week", "timezone": "America/New_York"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678488740500"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "week", "timezone": "+05:00"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678406400000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "week", "timezone": "America/New_York"  }','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"  } } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1678406400000"} } , "endDate": {"$date": {"$numberLong": "1678924740000"} }, "unit": "week"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1604255016000"} } , "endDate": {"$date": {"$numberLong": "1604275200000"} }, "unit": "week"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1604255016000"} } , "endDate": {"$date": {"$numberLong": "1604275200000"} }, "unit": "week", "timezone": "America/New_York"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"} } }');
-- weeks in 2024 year
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1704067200000"} } , "endDate": {"$date": {"$numberLong": "1735603200000"} }, "unit": "week"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1704067200000"} } , "endDate": {"$date": {"$numberLong": "1735603200000"} }, "unit": "week", "timezone": "America/Denver"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "timezone": "$timezone"} } }');

-- week startOfWeek cases
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1611512616000"} } , "endDate": {"$date": {"$numberLong": "1611541416000"} }, "unit": "week", "startOfWeek": "MONDAY"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "startOfWeek": "$startOfWeek"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1611512616000"} } , "endDate": {"$date": {"$numberLong": "1611541416000"} }, "unit": "week", "startOfWeek": "monday"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "startOfWeek": "$startOfWeek"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1611512616000"} } , "endDate": {"$date": {"$numberLong": "1611541416000"} }, "unit": "week", "startOfWeek": "tuesday"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "startOfWeek": "$startOfWeek"} } }');
select * from bson_dollar_project('{"startDate": {"$date": {"$numberLong": "1610859600000"} } , "endDate": {"$date": {"$numberLong": "1610859540000"} }, "unit": "week", "startOfWeek": "sunday", "timezone": "America/New_York"}','{"result": {"$dateDiff": {"startDate": "$startDate" , "endDate": "$endDate", "unit": "$unit", "startOfWeek": "$startOfWeek", "timezone": "$timezone"} } }');

-- $dateDiff unit month
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1262304000000"} } , "endDate": {"$date": {"$numberLong": "1293840000000"} }, "unit": "month"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1262304000000"} } , "endDate": {"$date": {"$numberLong": "1309392000000"} }, "unit": "month"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1267401600000"} } , "endDate": {"$date": {"$numberLong": "1272585600000"} }, "unit": "month"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1267401600000"} } , "endDate": {"$date": {"$numberLong": "1272585600000"} }, "unit": "month"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "993859200000"} } , "endDate": {"$date": {"$numberLong": "1262304000000"} }, "unit": "month"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"endDate": {"$date": {"$numberLong": "993859200000"} } , "startDate": {"$date": {"$numberLong": "1262304000000"} }, "unit": "month"  } } }');

-- $dateDiff unit quarter
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1262304000000"} } , "endDate": {"$date": {"$numberLong": "1293840000000"} }, "unit": "quarter"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1262304000000"} } , "endDate": {"$date": {"$numberLong": "1309392000000"} }, "unit": "quarter"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1267401600000"} } , "endDate": {"$date": {"$numberLong": "1272585600000"} }, "unit": "quarter"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1267401600000"} } , "endDate": {"$date": {"$numberLong": "1272585600000"} }, "unit": "quarter"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "993859200000"} } , "endDate": {"$date": {"$numberLong": "1262304000000"} }, "unit": "quarter"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"endDate": {"$date": {"$numberLong": "993859200000"} } , "startDate": {"$date": {"$numberLong": "1262304000000"} }, "unit": "quarter"  } } }');

-- $dateDiff unit year
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1262304000000"} } , "endDate": {"$date": {"$numberLong": "1293840000000"} }, "unit": "year"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1262304000000"} } , "endDate": {"$date": {"$numberLong": "1309392000000"} }, "unit": "year"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1267401600000"} } , "endDate": {"$date": {"$numberLong": "1272585600000"} }, "unit": "year"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1267401600000"} } , "endDate": {"$date": {"$numberLong": "1272585600000"} }, "unit": "year"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "993859200000"} } , "endDate": {"$date": {"$numberLong": "1262304000000"} }, "unit": "year"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"endDate": {"$date": {"$numberLong": "993859200000"} } , "startDate": {"$date": {"$numberLong": "1262304000000"} }, "unit": "year"  } } }');

-- $dateDiff invalid cases
select * from bson_dollar_project('{}','{"result": {"$dateDiff": 1 } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": "abc" } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": ["abc"] } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {} } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": {"$date": {"$numberLong": "1262304000000"} } , "unit": "month"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"endDate": {"$date": {"$numberLong": "1262304000000"} } , "unit": "month"  } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"startDate": 1 , "unit": "month",  "endDate": {"$date": {"$numberLong": "1262304000000"} } } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"endDate": 1 , "unit": "month",  "startDate": {"$date": {"$numberLong": "1262304000000"} } } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"hex":1, "endDate": 1 , "unit": "month",  "startDate": {"$date": {"$numberLong": "1262304000000"} } } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"endDate": {"$date": {"$numberLong": "1262304000000"} } , "unit": "ashd",  "startDate": {"$date": {"$numberLong": "1262304000000"} } } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"endDate": {"$date": {"$numberLong": "1262304000000"} } , "unit": 1,  "startDate": {"$date": {"$numberLong": "1262304000000"} } } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"endDate": {"$date": {"$numberLong": "1262304000000"} } , "unit": "week",  "startDate": {"$date": {"$numberLong": "1262304000000"} }, "timezone" : 1 } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"endDate": {"$date": {"$numberLong": "1262304000000"} } , "unit": "week",  "startDate": {"$date": {"$numberLong": "1262304000000"} }, "timezone" : "abcd" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateDiff": {"endDate": {"$date": {"$numberLong": "1262304000000"} } , "unit": "week",  "startDate": {"$date": {"$numberLong": "1262304000000"} }, "timezone" : "UTC", "startOfWeek": "hello" } } }');


-- $dateFromString null cases
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": null } } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11", "format": null } } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11", "format": "%Y-%m-%d", "timezone": null } } }');
-- checking with undefined as well
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": {"$undefined": true} } } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11", "format": {"$undefined": true} } } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11", "format": "%Y-%m-%d", "timezone": {"$undefined": true} } } }');
-- onError null does not effect null response
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11", "format": "%Y-%m-%d", "timezone": "UTC", "onError": null} } }');

-- $dateFromString null cases with document
select * from bson_dollar_project('{"dateString": null}','{"result": {"$dateFromString": {"dateString": "$dateString" } } }');
select * from bson_dollar_project('{"dateString": "2021-01-11", "format": null }','{"result": {"$dateFromString": {"dateString": "$dateString", "format": "$format" } } }');
select * from bson_dollar_project('{"dateString": "2021-01-11", "format": "%Y-%m-%d", "timezone": null }','{"result": {"$dateFromString": {"dateString": "$dateString", "format": "$format", "timezone": "$timezone" } } }');
select * from bson_dollar_project('{"dateString": {"$undefined": true} }','{"result": {"$dateFromString": {"dateString": "$dateString"} } }');
select * from bson_dollar_project('{"dateString": "2021-01-11", "format": {"$undefined": true}}','{"result": {"$dateFromString": {"dateString": "$dateString", "format": "$format" } } }');
select * from bson_dollar_project('{"dateString": "2021-01-11", "format": "%Y-%m-%d", "timezone": {"$undefined": true}}','{"result": {"$dateFromString": {"dateString": "$dateString", "format": "$format", "timezone": "$timezone" } } }');
-- onError null does not effect null response
select * from bson_dollar_project('{"dateString": "2021-01-11", "format": "%Y-%m-%d", "timezone": "UTC", "onError": null}','{"result": {"$dateFromString": {"dateString": "$dateString", "format": "$format", "timezone": "$timezone", "onError": "$onError"} } }');

-- $dateFromString normal cases
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12:53:12.506Z", "format": "%Y-%m-%dT%H:%M:%S.%LZ"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12.53.12.506Z", "format": "%Y-%m-%dT%H.%M.%S.%LZ"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "ABCD 2021-01-11T12.53.12.506Z", "format": "ABCD %Y-%m-%dT%H.%M.%S.%LZ"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2025-12-31", "format": "%Y-%m-%d"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "20-1-1", "format": "%Y-%m-%d"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "Year 2025, Month 12, Day 31", "format": "Year %Y, Month %m, Day %d"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12:53:12.506Z"} } }');
-- millisecond can be of min 1 and max 3 digits
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12.53.12.5Z", "format": "%Y-%m-%dT%H.%M.%S.%LZ"} } }');
-- millisecond of 2 digits
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12.53.12.50Z", "format": "%Y-%m-%dT%H.%M.%S.%LZ"} } }');
-- iso date format
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "53.7.2017", "format": "%V.%u.%G"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "1.1.1", "format": "%V.%u.%G"} } }');
-- iso part only requires year
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2017", "format": "%G"} } }');
-- % format specifier parsing test
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "% 2021-01-11T12:53:12.506Z", "format": "%% %Y-%m-%dT%H:%M:%S.%LZ"} } }');

-- $dateFromString normal cases with document
select * from bson_dollar_project('{"dateString": "2021-01-11T12:53:12.506Z", "format": "%Y-%m-%dT%H:%M:%S.%LZ"}','{"result": {"$dateFromString": {"dateString": "$dateString" , "format": "$format"} } }');
select * from bson_dollar_project('{"dateString": "2021-01-11T12.53.12.506Z", "format": "%Y-%m-%dT%H.%M.%S.%LZ"}','{"result": {"$dateFromString": {"dateString": "$dateString" , "format": "$format"} } }');
select * from bson_dollar_project('{"dateString": "ABCD 2021-01-11T12.53.12.506Z", "format": "ABCD %Y-%m-%dT%H.%M.%S.%LZ"}','{"result": {"$dateFromString": {"dateString": "$dateString" , "format": "$format"} } }');
select * from bson_dollar_project('{"dateString": "2025-12-31", "format": "%Y-%m-%d"}','{"result": {"$dateFromString": {"dateString": "$dateString" , "format": "$format"} } }');
select * from bson_dollar_project('{"dateString": "20-1-1", "format": "%Y-%m-%d"}','{"result": {"$dateFromString": {"dateString": "$dateString" , "format": "$format"} } }');
select * from bson_dollar_project('{"dateString": "Year 2025, Month 12, Day 31", "format": "Year %Y, Month %m, Day %d"}','{"result": {"$dateFromString": {"dateString": "$dateString" , "format": "$format"} } }');
select * from bson_dollar_project('{"dateString": "2021-01-11T12:53:12.506Z"}','{"result": {"$dateFromString": {"dateString": "$dateString"} } }');
-- millisecond can be of min 1 and max 3 digits
select * from bson_dollar_project('{"dateString": "2021-01-11T12.53.12.5Z", "format": "%Y-%m-%dT%H.%M.%S.%LZ"}','{"result": {"$dateFromString": {"dateString": "$dateString" , "format": "$format"} } }');
-- millisecond of 2 digits
select * from bson_dollar_project('{"dateString": "2021-01-11T12.53.12.50Z", "format": "%Y-%m-%dT%H.%M.%S.%LZ"}','{"result": {"$dateFromString": {"dateString": "$dateString" , "format": "$format"} } }');
-- iso date format
select * from bson_dollar_project('{"dateString": "53.7.2017", "format": "%V.%u.%G"}','{"result": {"$dateFromString": {"dateString": "$dateString" , "format": "$format"} } }');
select * from bson_dollar_project('{"dateString": "1.1.1", "format": "%V.%u.%G"}','{"result": {"$dateFromString": {"dateString": "$dateString" , "format": "$format"} } }');
-- iso part only requires year
select * from bson_dollar_project('{"dateString": "2017", "format": "%G"}','{"result": {"$dateFromString": {"dateString": "$dateString" , "format": "$format"} } }');

-- $dateFromString cases with onNull
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": null, "format": "%Y-%m-%dT%H:%M:%S.%LZ", "onNull": "5"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": null, "format": "%Y-%m-%dT%H:%M:%S.%LZ", "onNull": 1} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": null, "format": "%Y-%m-%dT%H:%M:%S.%LZ", "onNull":  {"$date": {"$numberLong": "1678924740500"} }} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "$missing", "onNull": "$missing"} } }');
-- only when dateString is nullish value, onNull is considered
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2018-02-06T11:56:02Z", "format": null, "onNull": 1} } }');
-- onNull with document cases
select * from bson_dollar_project('{"dateString": null, "format": "%Y-%m-%dT%H:%M:%S.%LZ", "onNull": "5"}','{"result": {"$dateFromString": {"dateString": "$dateFromString", "format": "$format", "onNull": "$onNull"} } }');
select * from bson_dollar_project('{"dateString": null, "format": "%Y-%m-%dT%H:%M:%S.%LZ", "onNull": 1}','{"result": {"$dateFromString": {"dateString": "$dateString", "format": "$format", "onNull": "$onNull"} } }');
select * from bson_dollar_project('{"dateString": null, "format": "%Y-%m-%dT%H:%M:%S.%LZ", "onNull":  {"$date": {"$numberLong": "1678924740500"} }}','{"result": {"$dateFromString": {"dateString": "$dateString", "format": "$format", "onNull":  "$onNull"} } }');

-- $dateFromString cases with timezone
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12:53:12.506Z", "format": "%Y-%m-%dT%H:%M:%S.%LZ", "timezone": "UTC"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12:53:12.506Z", "format": "%Y-%m-%dT%H:%M:%S.%LZ", "timezone": "+01:00"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12:53:12.506Z", "format": "%Y-%m-%dT%H:%M:%S.%LZ", "timezone": "America/Denver"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12:53:12.506+130", "format": "%Y-%m-%dT%H:%M:%S.%L%Z"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12:53:12.506-130", "format": "%Y-%m-%dT%H:%M:%S.%L%Z"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "Year is 2021 , Month is 12 and Day is @ 12 with minute offset +560", "format": "Year is %Y , Month is %m and Day is @ %d with minute offset %Z"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12:53:12.506+0130", "format": "%Y-%m-%dT%H:%M:%S.%L%z"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12:53:12.506-01:30", "format": "%Y-%m-%dT%H:%M:%S.%L%z"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12:53:12.506 UTC", "format": "%Y-%m-%dT%H:%M:%S.%L %z"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12:53:12.506 MST", "format": "%Y-%m-%dT%H:%M:%S.%L %z"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2021-01-11T12:53:12.506 GMT-03:00", "format": "%Y-%m-%dT%H:%M:%S.%L %z"} } }');

-- $dateFromString invalid cases
select * from bson_dollar_project('{}','{"result": {"$dateFromString": "a" } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": 1 } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": 5} } }');
select * FROM bson_dollar_project('{"dates": { "startDate": "2024-04-15", "endDate": "2024-05-14" }}','{ "result": { "$dateFromString": {"dateString": "$dates", "format": "%Y-%m-%d"}}}');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2022-10-12", "format": 1} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2022-10-12", "format": " %Y-%m-%d"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2022-10-12", "format": " %Y-%m-%d"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2024 1002", "format": "%Y %j" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "Jenuery 1 2024", "format": "%B %d %Y" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "act 1 2024", "format": "%b %d %Y" } } }');

-- unkown argument in input
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2022-10-12", "format": " %Y-%m-%d", "unknown":1} } }');
-- passing timezone as input with existing timezone in string
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2017-07-12T22:23:55 GMT+02:00", "format": "%Y-%m-%dT%H:%M:%S %z", "timezone": "Europe/Paris"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2017-07-12T22:23:55 GMT", "format": "%Y-%m-%dT%H:%M:%S %z", "timezone": "Europe/Paris"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2017-07-12T22:23:55 +02:20", "format": "%Y-%m-%dT%H:%M:%S %z", "timezone": "Europe/Paris"} } }');
-- not enough data
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2017-07T22:23:55 GMT+02:00", "format": "%Y-%mT%H:%M:%S %z"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2017", "format": "%C"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "ISO Week 1, 2018", "format": "ISO Week %V, %Y"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "12/31/2018", "format": "%m/%d\\0/%Y"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2017-12", "format": "%Y"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "ISO Day 6", "format": "ISO Day %V"} }}');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "12/31/2018", "format": "%m/%d/%G"} } }');
-- % format specifier test
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "% 2021-01-11T12:53:12.506Z", "format": "% %Y-%m-%dT%H:%M:%S.%LZ"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "% 2021-01-11T12:53:12.506Z", "format": "%%Y-%m-%dT%H:%M:%S.%LZ"} } }');

-- $dateFromString cases with onError
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": "error handled","dateString": 5} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": "error handled","dateString": "2022-10-12", "format": " %Y-%m-%d"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": "error handled","dateString": "2022-10-12", "format": " %Y-%m-%d"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": "error handled","dateString": "2022-10-12", "format": null } } }');
-- if dateString is not a string, onError should be considered
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": "error handled","dateString": 5, "format": null } } }');
-- passing timezone as input with existing timezone in string
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": "error handled","dateString": "2017-07-12T22:23:55 GMT+02:00", "format": "%Y-%m-%dT%H:%M:%S %z", "timezone": "Europe/Paris"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": "error handled","dateString": "2017-07-12T22:23:55 GMT", "format": "%Y-%m-%dT%H:%M:%S %z", "timezone": "Europe/Paris"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": "error handled", "dateString": "2017-07-12T22:23:55 +02:20", "format": "%Y-%m-%dT%H:%M:%S %z", "timezone": "Europe/Paris"} } }');
-- not enough data
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": "error handled","dateString": "2017-07T22:23:55 GMT+02:00", "format": "%Y-%mT%H:%M:%S %z"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": "error handled","dateString": "ISO Week 1, 2018", "format": "ISO Week %V, %Y"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": "error handled","dateString": "12/31/2018", "format": "%m/%d\\0/%Y"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": "error handled","dateString": "2017-12", "format": "%Y"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": "error handled","dateString": "ISO Day 6", "format": "ISO Day %V"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": "error handled","dateString": "12/31/2018", "format": "%m/%d/%G"} } }');
-- checking with changing onError return type
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": {"$date": {"$numberLong": "1678924740500"} },"dateString": 5} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": {"$date": {"$numberLong": "1678924740500"} },"dateString": "2022-10-12", "format": " %Y-%m-%d"} } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"onError": {"$date": {"$numberLong": "1678924740500"} },"dateString": "2022-10-12", "format": " %Y-%m-%d"} } }');
-- adding extra format specifiers
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2024 234 04:34:11", "format": "%Y %j %H:%M:%S" } } }');
-- exception test for value of dayOfyear is 999
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "2024 999", "format": "%Y %j" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "year is 2024 and the days are 234 and time is 04:34:11", "format": "year is %Y and the days are %j and time is %H:%M:%S" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "dec 1 2024 04:34:11", "format": "%b %d %Y %H:%M:%S" } } }');
select * from bson_dollar_project('{}','{"result": {"$dateFromString": {"dateString": "janUary 1 2024 04:34:11", "format": "%B %d %Y %H:%M:%S" } } }');

-- Defensive check to prevent error from ms conversion to timestamptz
SELECT * FROM bson_dollar_project('{}', '{"result": {"$dateToString": {"date":{"$toDate": {"$numberDouble": "-244058800000000"}}}}}');
SELECT * FROM bson_dollar_project('{}', '{"result": {"$dateToString": {"date":{"$toDate": {"$numberDouble": "9664590969990000"}}}}}');

-- parse with preset formats
select * from bson_dollar_project('{"dateString": "2021-01-11 11:00:03"}','{"result": {"$dateFromString": {"dateString": "$dateString"} } }');
select * from bson_dollar_project('{"dateString": "2021-01-11T12:53:12"}','{"result": {"$dateFromString": {"dateString": "$dateString"} } }');
select * from bson_dollar_project('{"dateString": "July 4th, 2017"}','{"result": {"$dateFromString": {"dateString": "$dateString"} } }');
select * from bson_dollar_project('{"dateString": "July 4th, 2017 12:39:30 BST"}','{"result": {"$dateFromString": {"dateString": "$dateString"} } }');
select * from bson_dollar_project('{"dateString": "July 4th, 2017 11am"}','{"result": {"$dateFromString": {"dateString": "$dateString"} } }');
select * from bson_dollar_project('{"dateString": "2017-Jul-04 noon"}','{"result": {"$dateFromString": {"dateString": "$dateString"} } }');

