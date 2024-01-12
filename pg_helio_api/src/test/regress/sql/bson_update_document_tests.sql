SET search_path TO helio_api,helio_core;
SET helio_api.next_collection_id TO 2000;
SET helio_api.next_collection_index_id TO 2000;

-- replace document scenarios
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "a": 1}', '{ "": { "_id": 1, "b": 2 } }', '{}');

-- allow updating ID if it's type compatible
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "a": 1}', '{ "": { "_id": 1.0, "b": 2 } }', '{}');

-- ID mismatch not allowed: error
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "a": 1}', '{ "": { "_id": 2.0, "b": 2 } }', '{}');

-- ID not specified - pick the ID from the source document
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "a": 1}', '{ "": { "b": 2 } }', '{}');

-- upsert case: Creates a new document.
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "_id": 2.0, "b": 2 } }', '{}');

-- upsert case: no id specified creates a new ID
SELECT (helio_api_internal.bson_update_document('{}', '{ "": { "b": 2 } }', '{}')).newDocument @? '{ "_id" : 1 }';

-- upsert case: no id specified creates from filter
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "b": 2 } }', '{"$and": [ { "a": 1, "_id": 2.0 }]}');

-- upsert case: no id specified autogenerates when _id filter is not $eq.
SELECT (helio_api_internal.bson_update_document('{}', '{ "": { "b": 2 } }', '{"$and": [ { "a": 1, "_id": { "$gt": 2.0 } }]}')).newDocument  @? '{ "_id" : 1 }';
SELECT (helio_api_internal.bson_update_document('{}', '{ "": { "b": 2 } }', '{"$and": [ { "a": 1, "_id": { "$lt": 2.0 } }]}')).newDocument  @? '{ "_id" : 1 }';
SELECT (helio_api_internal.bson_update_document('{}', '{ "": { "b": 2 } }', '{"$or": [ { "a": 1, "_id": 2.0 } ]}')).newDocument  @? '{ "_id" : 1 }';
SELECT (helio_api_internal.bson_update_document('{}', '{ "": { "b": 2 } }', '{"$or": [ { "a": 1 }, { "_id": 3 } ] }')).newDocument @? '{ "_id" : 1 }';

-- would be nice to use the $type function.
SELECT bson_get_value((helio_api_internal.bson_update_document('{}', '{ "": { "b": 2 } }', '{"$or": [ { "a": 1, "_id": 2.0 } ]}')).newDocument, '_id') @=  '{ "" : 2.0 }';
SELECT bson_get_value((helio_api_internal.bson_update_document('{}', '{ "": { "b": 2 } }', '{"$or": [ { "a": 1 }, { "_id": 3 } ] }')).newDocument, '_id') @!= '{ "" : 3 }';

-- upsert case: _id collision between query and replace; errors out
SELECT helio_api_internal.bson_update_document('{}', '{ "": { "_id": 3, "b": 2 } }', '{"$and": [ { "a": 1, "_id": 2.0 }]}');
SELECT helio_api_internal.bson_update_document('{}', '{ "": { "$setOnInsert": {"_id.b": 1}, "$set":{ "_id": 1 } } }', '{"_id.a": 4}');

-- _id specified twice, failure for update.
SELECT helio_api_internal.bson_update_document('{}', '{ "": { "_id": 3, "b": 2 } }', '{"$and": [ { "a": 1 }, {"_id": 2.0 }, { "_id": 3.0 }]}');

-- update scenario tests: $set
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1 }', '{ "": { "$set": { "a": 2 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1, 2, 3 ] }', '{ "": { "$set": { "a": 2 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1, 2, 3 ] }', '{ "": { "$set": { "a.1": "somePath" } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": { "c": 3 }} }', '{ "": { "$set": { "a.b.c": "somePath" } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": [1, 2, 3 ]} }', '{ "": { "$set": { "a.b.7": 9, "a.b.10": 13 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": [1, 2, 3 ]} }', '{ "": { "$set": { "a.b.7": 9, "a.b.10": 13 }, "$unset": { "a.b.6": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": [1, 2, 3 ]} }', '{ "": { "$set": { "a.b.1": { "sub": "1234" }, "a.b.5": [1, 2, 3 ] } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 2 }', '{ "": { "$set": { "b.c.d": { "sub": "1234" } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": { "c": 3 }} }', '{ "": { "$set": { "a.b.c": "somePath", "a.b.c": false } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [] }', '{ "": { "$set": { "a.2.b": 1, "a.2.c": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [] }', '{ "": { "$set": { "a.1500001": 1 } } }', '{}');

-- update scenario tests: $inc
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$inc": { "a.b": 2 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": { "c": 3 }} }', '{ "": { "$set": { "a.b.c": "somePath", "a.b.c": 10 }, "$inc": { "a.b.c": 2 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": 2 }', '{ "": { "$set": { "b.c": 10 }, "$inc": { "b.c": 2 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 2 }', '{ "": { "$inc": { "b.c": 2 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [ 1, 2, 3 ] }', '{ "": { "$inc": { "a.2": 2, "a.6": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": {"$numberLong": "9223372036854775807"} }', '{ "": { "$inc": { "a": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": {"$numberLong": "-9223372036854775808"} }', '{ "": { "$inc": { "a": -1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id":{"$oid":"5d505646cf6d4fe581014ab2"}, "a": {"$numberLong": "9223372036854775807"} }', '{ "": { "$inc": { "a": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": {"$numberDouble": "9223372036854775807"} }', '{ "": { "$inc": { "a": 1 } } }', '{}');

-- update scenario tests: $min
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$min": { "a.b": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$min": { "a.b": true } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$min": { "a.b": null } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$min": { "a.b": [1, 2, 3 ] } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$min": { "a.c": [1, 2, 3 ] } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1, 2, 3 ] }', '{ "": { "$min": { "a.1": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1, 2, 3 ] }', '{ "": { "$min": { "a.1": 3 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1, 2, 3 ] }', '{ "": { "$set": { "a.1": -1 }, "$min": { "a.1": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$min": { "a.b": {"$numberDecimal": "9.99e-100"} } } }', '{}');

-- update scenario tests: $max
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$max": { "a.b": 3 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$max": { "a.b": true } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$max": { "a.b": null } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$max": { "a.b": [1, 2, 3 ] } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$max": { "a.c": [1, 2, 3 ] } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1, 2, 3 ] }', '{ "": { "$max": { "a.1": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1, 2, 3 ] }', '{ "": { "$max": { "a.1": 3 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1, 2, 3 ] }', '{ "": { "$set": { "a.1": 3 }, "$max": { "a.1": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$max": { "a.b": {"$numberDecimal": "9.99e100"} } } }', '{}');

-- update scenario tests: $bit
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "expdata" :13}', '{ "": { "$bit": { "expdata" : {  "and" : 10 }} }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "expdata" :3}', '{ "": { "$bit": { "expdata" : {  "or" : 5 }} }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "expdata" :1}', '{ "": { "$bit": { "expdata" : {  "xor" : 5 }} }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "expdata" :[13,3,1]}', '{ "": { "$bit": { "expdata.0" : { "and" :10 }  }  }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "expdata" :13}', '{ "": { "$bit": { "expdata" : { "and" : 0, "or" : 10 }} }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "expdata" :13}', '{ "": { "$bit": { "expdata" : { "or" : 10, "xor" : 10 }} }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1}', '{ "": { "$bit": { "expdata" : { "or" : 10 }} }, "upsert" : false}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "key": { "x": { "y": [ 100, 200 ]}}, "f": 12 }', '{ "": { "$bit": {"key.x.y.0": {"and": 10}, "f": {"and": 10 }}}}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "key": { "x": { "y": 10 }}, "f": 1}', '{ "": { "$bit": { "key.x.y": { "xor": 10 } }}}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "key": {"x": { "y": [100 , 200 ] } }, "f": 1 } ', '{ "": { "$bit": { "key.x.y.0": { "and": 10 } }}}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "x": 1 , "f": 1 } ', '{ "": { "$bit": { "x": { "and": { "$numberLong": "1" } } } } }', '{}');


-- update scenario negative tests: $bit
SELECT helio_api_internal.bson_update_document('{"_id": 1, "expdata" :"abc"}', '{ "": { "$bit": { "expdata" : { "or" :10 }  }  }}', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "expdata" :13}', '{ "": { "$bit": { "expdata" : { "or" :10.0 }  }  }}', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "expdata" :13}', '{ "": { "$bit": { "expdata" : { "ors" :10 }  }  }}', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "expdata" :[1,2,3]}', '{ "": { "$bit": { "expdata" : { "or" :10 }  }  }}', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "expdata" :13}', '{ "": { "$bit": { "expdata" : {  }} }}', '{}');
/* TODO: Make the error compatible with mongo: "errmsg" : "Cannot create field 'y' in element {x: [ { y: 10 } ]}"  */
SELECT helio_api_internal.bson_update_document('{"_id": 1, "key": {"x": [ { "y": 10 } ] }}', '{ "": { "$bit": { "key.x.y": { "and": 0 }}}}', '{}');


-- update scenario tests: $addToSet
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id":1 }', '{ "": {"$addToSet": { "letters": "a" } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1 }', '{ "": {"$addToSet": { "letters": {"a":"b", "c":"d"} } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1 }', '{ "": {"$addToSet": { "letters": {"$each" : ["a", "b","c"]} } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "letters": ["a", "b"]}', '{ "": {"$addToSet": { "letters": {"c":"d", "e":"f", "g":"h" }} }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "letters": ["a", "b"]}', '{ "": {"$addToSet": { "letters": "c" } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "letters": ["a", "b"]}', '{ "": {"$addToSet": { "letters": ["c", "d"] } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "letters": ["a", "b"]}', '{ "": {"$addToSet": { "letters": "a" } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "letters": ["a", "b"]}', '{ "": {"$addToSet": { "letters": ["a", "b"] } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "letters": ["a", "b"]}', '{ "": {"$addToSet": { "c":123456} }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "colors": "blue, green, red" }', '{ "": { "$addToSet": { "newColors": "mauve" } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "colors": ["blue, green, red"] }', '{ "": { "$addToSet": { "colors": {} } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "colors": ["blue, green, red", { }] }', '{ "": { "$addToSet": { "colors": {} } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "letters": ["a","b"] }', '{ "": { "$addToSet": { "letters": { "$each": ["c", "d"]}}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "letters": ["a","b"] }', '{ "": { "$addToSet": { "letters": { "$each": [ { } ]}}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "numbers": [44,78,80,80,80] }', '{ "": { "$addToSet": { "numbers": 80 }} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "letters": ["a","b"] }', '{ "": { "$addToSet": { "letters": { "$each" : ["c","d","c","d","c","e","d","e"] }}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "letters": ["a","b","e"] }', '{ "": { "$addToSet": { "letters": { "$each" : ["a","b","c","d","e"] }}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "numbers": [1,2,5] }', '{ "": { "$addToSet": { "numbers": { "$each" : [1,2,3,4,5,5,4,3,2,1] }}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "key": {"x": { "y": [100 , 200 ] } }, "f": 1 } ', '{ "": { "$addToSet": { "key.x.y": { "$each": [3, 5, 6] } }}}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "key": {"x": { "y": [100 , 200, "a" ] } }, "f": 1 } ', '{ "": { "$addToSet": { "key.x.y": { "$each": [3, 5, 6, 7,8,9,"a","b","a",7,8] } }}}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "key": { "x": { "y": [ 100,3,6 ]}}, "f": [1,3] }', '{ "": { "$addToSet": {"key.x.y": {"$each": [1, 3, 6, 9,"b","a"]}, "f": {"$each": [1,2,4] }}}}', '{}');



-- Update scenario for some complex items: $addToSet
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id" : 1, "letters" : [ [ 1, { "a" : 2 } ], { "a" : [ 1, 2 ] } ] }', '{ "": { "$addToSet": { "letters": { "$each": [1,3,4] } } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id" : 1, "letters" : [ [ 1, { "a" : 2 } ], { "a" : [ 1, 2 ] } ] }', '{ "": { "$addToSet": { "letters": { "$each": [[1,3,4]] } } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id" : 1, "letters" : [ [ 1, { "a" : 2 } ], { "a" : [ 1, 2 ] } ] }', '{ "": { "$addToSet": { "letters.0": { "$each": [1,3,4] } }}}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id" : 1, "letters" : [ [ 1, { "a" : 2 } ], { "a" : [ 1, 2 ] } ] }', '{ "": {"$addToSet": { "letters.0": { "$each": [1,3,4, { "a":2}, {"a":3} ] } }}}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id" : 1, "letters" : [ [ 1, { "a" : 2 } ], { "a" : [ 1, 2 ] } ] }', '{ "": {"$addToSet": { "letters.0": { "$each": [4,{"a":3} ] }, "letters.1.a": { "$each": [4, {"a":2}] }  } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3]}', '{ "": { "$addToSet" : {"a.3": 4 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3]}', '{ "": { "$addToSet" : {"a.7": 7 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [[1],[2],[3]]}', '{ "": { "$addToSet" : {"a.0.5": 7 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [[1],[2],3]}', '{ "": { "$addToSet" : {"a.2.5": 7 } } }', '{}');

-- update scenario negative tests: $inc
SELECT helio_api_internal.bson_update_document('{"_id": 5, "a": [1,2] }', '{ "": { "$inc": { "a": 30 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 5, "a": {"x":1} }', '{ "": { "$inc": { "a": 30 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 5, "a": "b" }', '{ "": { "$inc": { "a": 30 } } }', '{}');

-- update scenario negative tests: $addToSet
SELECT helio_api_internal.bson_update_document('{"_id": 1, "colors": "blue, green, red" }', '{ "": { "$addToSet": { "colors": "mauve" } }}', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "letters": ["a","b"] }', '{ "": { "$addToSet": { "letters": { "$each": "c"}}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "letters": ["a","b"] }', '{ "": { "$addToSet": { "letters": { "$each" : {} }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "letters": ["a","b"] }', '{ "": { "$addToSet": { "letters": { "$ach": ["c"]}}} }', '{}');


-- update scenario tests: $unset
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$unset": { "a.b": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2, "c": 3 }, "d": 1 }', '{ "": { "$unset": { "a.b": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": [ 1, 2, 3 ] } }', '{ "": { "$unset": { "a.b.1": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": [ 1, 2, 3 ] } }', '{ "": { "$unset": { "a.b.5": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1 }', '{ "": { "$unset": { "a": 1, "b.c": 1 } } }', '{}');


-- update scenario tests: $rename
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$rename": { "a.b": "c.d" } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$rename": { "a.b": "a.c" } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$rename": { "a": "c" } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "a": 1 }', '{ "" : { "$rename": { "b": "c" } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "a": 1, "c": 4}', '{ "": { "$rename": { "b": "c.d" } } }', '{}');
-- $rename when rename source node doesn't exist but target node exist, this should be a no-op
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "a": 1, "c": 4}', '{ "": { "$rename": { "b": "a" } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "a": {"b": 1, "d": 2}}', '{ "": { "$rename": { "a.b": "c" } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "a": {"b": 1}}', '{ "": { "$rename": { "a.b": "d" } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "a": 1}', '{ "": { "$rename": { "b": "a" }, "$set": {"x" : 1} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "a": 1, "x": 1}', '{ "": { "$rename": { "b": "a" }, "$inc": {"x" : 1} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "b": 1, "x": 1}', '{ "": { "$rename": { "a": "b", "x": "y" } } }', '{}');


-- update scenario tests: $mul
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberDouble" : "2.0"} } }', '{ "": { "$mul": { "a.b": 0 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberInt" : "2" } } }', '{ "": { "$mul": { "a.b": 0 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberLong" : "2" } } }', '{ "": { "$mul": { "a.b": 0 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberLong" : "9223372036854775807" } } }', '{ "": { "$mul": { "a.b": {"$numberLong": "0"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberDouble" : "0.0"} } }', '{ "": { "$mul": { "a.b": 10 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberInt" : "0" } } }', '{ "": { "$mul": { "a.b": 10 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberLong" : "0" } } }', '{ "": { "$mul": { "a.b": 10 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$mul": { "a.b": 10 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$mul": { "a.b": -10 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$mul": { "a.b": {"$numberDouble": "+1.0"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$mul": { "a.b": 1.1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$mul": { "a.b": -1.1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$mul": { "a.c": {"$numberInt": "2"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$mul": { "a.c": {"$numberLong": "2"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$mul": { "a.c": {"$numberDouble": "2"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$mul": { "a.c": {"$numberDecimal": "2"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberInt": "100"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberInt": "500"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberInt": "2147483647"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberInt": "2147483647"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberInt": "2147483647"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberLong": "2147483647"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberInt": "2147483647"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDouble": "2.0"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberLong": "2147483647"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberInt": "-2147483647"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberLong": "2147483647"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberLong": "-2147483647"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberLong": "9223372036854775807"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDouble": "2.0"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberDouble": "10.0"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberInt": "2"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberDouble": "10.0"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberLong": "2"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberDouble": "10.0"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDouble": "2.0"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": NaN } }','{ "": { "$mul": { "a.b": 2 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }','{ "": { "$mul": { "a.b": NaN } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberDecimal": "10"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberInt": "2"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberInt": "10"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDecimal": "2"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberDecimal": "10"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberLong": "2"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberLong": "10"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDecimal": "2"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberDecimal": "10"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDouble": "2"} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberDouble": "10"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberDecimal": "2"} } } }', '{}');
-- int64 overflow should coerce to double: $mul
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberLong": "9223372036854775807"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberInt": "2"} } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {"$numberLong": "-9223372036854775807"} } }',
                                                '{ "": { "$mul": { "a.b": {"$numberInt": "2"} } } }', '{}');

-- update scenario error tests: $mul
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }','{ "": { "$mul": { "a.b": "Hello" } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": "Text" } }','{ "": { "$mul": { "a.b": 2 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": [1,2,3] } }','{ "": { "$mul": { "a.b": 2 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": {} } }','{ "": { "$mul": { "a.b": 2 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": null } }','{ "": { "$mul": { "a.b": 2 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": true } }','{ "": { "$mul": { "a.b": 2 } } }', '{}');


-- $pullAll operator- positive testcases
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id" : 1, "scores" : [ 0, 2, 5, 5, 1, 0 ] }', '{ "": { "$pullAll": { "scores": [0,5] } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id" : 1, "letters" : [ [ 1, { "a" : 2 } ], { "a" : [ 1, 2 ] } ] }', '{ "": { "$pullAll": { "letters": [1,3,4] } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id" : 1, "letters" : [ [ 1, { "a" : 2 } ], { "a" : [ 1, 2 ] } ] }', '{ "": { "$pullAll": { "letters.0": [1,3,4] } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id" : 1, "scores" : [ 0, 2, 5, 5, 1, 0 ] }', '{ "": { "$pullAll": { } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id" : 1, "scores" : [ 0, 2, 5, 5, 1, 0 ] }', '{ "": { "$pullAll": { "score": [0,5] } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id" : 1, "scores" : [ 0, 2, 5, 5, 1, 0 ] }', '{ "": { "$pullAll": { "a.b": [0,5] } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id" : 1, "scores" : {} }', '{ "": { "$pullAll": { "scores.top": [0,5] } }}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id" : 1, "scores" : {"top": [0,5,10,11]} }', '{ "": { "$pullAll": { "scores.top": [0,5] } }}', '{}');


-- $pullAll operator- negative testcases
SELECT helio_api_internal.bson_update_document('{"_id" : 1, "scores" : [ 0, 2, 5, 5, 1, 0 ] }', '{ "": { "$pullAll": { "scores": 0 } }}', '{}');
SELECT helio_api_internal.bson_update_document('{"_id" : 1, "scores" : "a" }', '{ "": { "$pullAll": { "scores": [0] } }}', '{}');
SELECT helio_api_internal.bson_update_document('{"_id" : 1, "letters" : [ [ 1, { "a" : 2 } ], { "a" : [ 1, 2 ] } ] }', '{ "": { "$pullAll": { "letters.1": [1,3,4] } }}', '{}');


-- rename overwrites.
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2, "c": 1 } }', '{ "": { "$rename": { "a.b": "a.c" } } }', '{}');

-- update scenario tests: $setOnInsert (non-upsert)
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$setOnInsert": { "a.b": 1, "c": 2, "d": 4 } } }', '{}');

-- upsert:
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 2, "_id": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 2 } } }', '{ "_id": 1 }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 2 } } }', '{ "_id": { "$eq": 1 } }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 2, "h": 6 } } }', '{ "_id": { "$eq": 1 }, "f": 3, "g": 4, "h": 5 }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 2, "h": 6 } } }', '{ "$and": [ { "_id": { "$eq": 1 }}, {"f": 3 }, { "g": 4 }, { "h": 5 } ] }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 2, "h": 6 } } }', '{ "_id": { "$eq": 1 }, "$or": [ { "_id": { "$eq": 1 }}, {"f": 3 }, { "g": 4 }, { "h": 5 } ] }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 2, "h": 6 } } }', '{ "_id": { "$eq": 1 }, "$or": [ { "g": 4 } ] }');

--upsert querySpec has document
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 5, "_id": 1 } } }', '{"a.x.y":10}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a.y": 5, "_id": 1 } } }', '{"a.x":{"c":"d"}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a.y": 10, "_id": 1 } } }', '{"a": {"x":5}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": {"b":5}, "_id": 1 } } }', '{"a.c":8}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": {"x":{"z":10}}, "_id": 1} }}', '{"a":{"x":{"y":5}}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a.x.z": 10, "_id": 1 } } }', '{"a":{"x":{"y":5}}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": {"x":{"z":10}}, "_id": 1 } } }', '{"a.x.y":10}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a.x.z":10, "_id": 1 } } }', '{"a.x.y":10}');


--upsert querySpec has array
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a.11":11, "_id": 1 } } }', '{"a":[0,1,2,3,4,5,6,7,8,9,10]}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a.0":0, "a.6":6, "_id": 1 } } }', '{"a":[8,1,2,3]}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a.0":1, "a.1":2, "_id": 1 } } }', '{"a":[{"x":1},{"x":2},{"x":3}]}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a.0.1": 5 , "_id": 1} } }', '{"a":[[1,2],[3,4]]}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a.0.x": 10 , "_id": 1} } }', '{"a":[{"x":1},{"x":2}]}');

--upsert $rename cases
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$rename": { "a": "b"}, "$set":{ "_id": 1 } } }', '{"b":1}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$rename": { "b": "a"}, "$set":{ "_id": 1 } } }', '{"b":1}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$rename": { "a.b": "a.c"}, "$set":{ "_id": 1 } } }', '{"a.b":10}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$rename": { "a.b": "a.c"}, "$set":{ "_id": 1 } } }', '{"a":{"b":10}}');
--upsert querySpec with $gt, $lt, $gte, $lte
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "_id":1} } }', '{"a.1":{"$gt": 100},"a.2":{"x":10}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "_id":1} } }', '{"a.x":{"$lt": 100},"a.y":{"x":10}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "_id":1} } }', '{"a.x":{"$gte": 100},"a.y":{"x":10}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "_id":1} } }', '{"a.x":{"$lte": 100},"a.y":{"x":10}}');

-- upsert querySpec with implicit AND - valid cases
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "_id": 1, "a": 1} } }', '{"b": { "$eq": 2, "$in": [ 1, 2 ] } }'); 
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "_id": 1, "a": 1} } }', '{"b": { "$eq": 2, "$gt": 20, "$lt": 40 } }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "_id": 1, "a": 1} } }', '{"b": { "$all": [ 2 ], "$in": [ 1, 2 ] } }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "_id": 1, "a": 1} } }', '{"b": { "$all": [ 2 ], "$ne": 40 } }');

-- upsert querySpec with implicit AND - Invalid cases
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "_id": 1, "a": 1} } }', '{"b": { "$eq": 2, "$all": [ 2 ] } }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "_id": 1, "a": 1} } }', '{"b": { "$eq": 2, "$all": [ 2 ], "$in" : [ 2 ] } }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "_id": 1, "a": 1} } }', '{"b": { "$all": [ 2, 1 ], "$in": [ 1, 2 ] } }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "_id": 1, "a": 1} } }', '{"b": { "$all": [ 2, 1 ], "$in": [ 2 ] } }');

-- upsert querySpec with implicit AND on "_id" - valid cases
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 1} } }', '{"_id": { "$eq": 2, "$in": [ 1, 2 ] } }'); 
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 1} } }', '{"_id": { "$eq": 2, "$gt": 20, "$lt": 40 } }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 1} } }', '{"_id": { "$all": [ 2 ], "$in": [ 1, 2 ] } }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 1} } }', '{"_id": { "$all": [ 2 ], "$ne": 40 } }');

-- upsert querySpec with implicit AND on "_id" - Invalid cases
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 1} } }', '{"_id": { "$eq": 2, "$all": [ 2 ] } }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 1} } }', '{"_id": { "$eq": 2, "$all": [ 2 ], "$in" : [ 2 ] } }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 1} } }', '{"_id": { "$all": [ 2, 1 ], "$in": [ 1, 2 ] } }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 1} } }', '{"_id": { "$all": [ 2, 1 ], "$in": [ 2 ] } }');


-- cannot modify id cases
SELECT helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 2, "_id": 2 } } }', '{ "_id": 1 }');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": 1 }', '{ "": { "$set": { "_id": 2 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": 1 }', '{ "": { "$inc": { "_id": 2 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": 1 }', '{ "": { "$max": { "_id": 2 } } }', '{}');

-- Multiple operators: no-op
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": {}, "x": 0 }', '{ "": { "$pullAll": { "a.b": [0] }, "$unset": { "d": 1 }, "$mul": { "x": 10 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": {}, "x": 0 }', '{ "": { "$pullAll": { "a.b": [0] }, "$unset": { "d": 1 }, "$bit": { "x": { "and": 0 } } } }', '{}');

-- Multiple operators at least one update
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [0,0,0,0,4], "x": 0 }', '{ "": { "$pullAll": { "a": [0] }, "$unset": { "d": 1 }, "$mul": { "x": 10 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [0,0,0,0,4], "x": 10 }', '{ "": { "$pullAll": { "b": [0] }, "$unset": { "a": 1 }, "$mul": { "x": 10 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": {"b": [0,0,0,0,0,1,1,1,2]}, "x": 10 }', '{ "": { "$pullAll": { "a.b": [0,1] }, "$set": { "d": 1 }, "$mul": { "x": 1 } } }', '{}');

-- aggregation pipeline: project & unset

-- -- simple case for project
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1, "b": 2 }', '{ "": [ { "$project": { "a": 1, "c": { "$literal": 2.0 }} }] }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1, "b": 2 }', '{ "": [ { "$project": { "b": 0 } }] }', '{}');

SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "x": 1, "y": [{ "z": 1, "foo": 1 }] }', '{ "": [ { "$unset": ["x", "y.z"] } ] }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 2, "title": "Bees Babble", "isbn": "999999999333", "author": { "last": "Bumble", "first": "Bee" }, "copies": [{ "warehouse": "A", "qty": 2 }, { "warehouse": "B", "qty": 5 }] }',
                                               '{ "": [ { "$unset": "copies" } ] }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 2, "title": "Bees Babble", "isbn": "999999999333", "author": { "last": "Bumble", "first": "Bee" }, "copies": [{ "warehouse": "A", "qty": 2 }, { "warehouse": "B", "qty": 5 }] }',
                                               '{ "": [ { "$unset": ["isbn", "author.first", "copies.warehouse"] } ] }', '{}');

SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "name": "John Doe" }',
                                               '{ "": [ { "$project": { "_id": 1, "name": 1 } }, { "$unset": "name" } ] }', '{}');

-- -- project with upsert uses fields from query
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": [ { "$project": { "b": { "$literal": 2.0 }, "d": 1 } }] }', '{ "d" : 1, "_id": 2.0 }');

-- -- project and unset (shoudl just project _id )
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1, "b": 2 }', '{ "": [ { "$unset": "b" }, { "$project": { "b": 1 } }] }', '{}');

-- project and unset silently ignore removing the _id field.
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1, "b": 2 }', '{ "": [ { "$unset": "_id" }] }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1, "b": 2 }', '{ "": [ { "$project": { "_id": 0 } } ] }', '{}');

-- -- simple case for addFields
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1, "b": 2 }', '{ "": [ { "$addFields": { "a": 1, "c": { "$literal": 2.0 }} }] }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1, "b": 2 }', '{ "": [ { "$addFields": { "b": 0 } }] }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "name": "John Doe" }', '{ "": [ { "$addFields": { "_id": 1, "name": 1 } }, { "$unset": "name" } ] }', '{}');
SELECT helio_api_internal.bson_update_document('{ "_id": 1, "name": "John Doe" }', '{ "": [ { "$addFields": { "_id": 10, "name": 1 } }, { "$unset": "name" } ] }', '{}');
SELECT helio_api_internal.bson_update_document('{ "_id": 1, "name": "John Doe" }', '{ "": [ { "$addFields": { "_id": 30.5, "name": 1 } }, { "$unset": "name" } ] }', '{}');
SELECT helio_api_internal.bson_update_document('{ "_id": 1, "name": "John Doe" }', '{ "": [ { "$addFields": { "_id": false, "name": 1 } }, { "$unset": "name" } ] }', '{}');
SELECT helio_api_internal.bson_update_document('{ "_id": 1, "name": "John Doe" }', '{ "": [ { "$addFields": { "_id": "someString", "name": 1 } }, { "$unset": "name" } ] }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": [ { "$addFields": { "b": { "$literal": 2.0 }, "d": 1 } }] }', '{ "d" : 1, "_id": 2.0 }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1, "b": 2 }', '{ "": [ { "$unset": "b" }, { "$addFields": { "b": 1 } }] }', '{}');


-- -- simple case for set
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1, "b": 2 }', '{ "": [ { "$set": { "a": 1, "c": { "$literal": 2.0 }} }] }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1, "b": 2 }', '{ "": [ { "$set": { "b": 0 } }] }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{ "_id": 1, "name": "John Doe" }', '{ "": [ { "$set": { "_id": 1, "name": 1 } }, { "$unset": "name" } ] }', '{}');
SELECT helio_api_internal.bson_update_document('{ "_id": 1, "name": "John Doe" }', '{ "": [ { "$set": { "_id": 10, "name": 1 } }, { "$unset": "name" } ] }', '{}');
SELECT helio_api_internal.bson_update_document('{ "_id": 1, "name": "John Doe" }', '{ "": [ { "$set": { "_id": 30.5, "name": 1 } }, { "$unset": "name" } ] }', '{}');
SELECT helio_api_internal.bson_update_document('{ "_id": 1, "name": "John Doe" }', '{ "": [ { "$set": { "_id": false, "name": 1 } }, { "$unset": "name" } ] }', '{}');
SELECT helio_api_internal.bson_update_document('{ "_id": 1, "name": "John Doe" }', '{ "": [ { "$set": { "_id": "someString", "name": 1 } }, { "$unset": "name" } ] }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": [ { "$set": { "b": { "$literal": 2.0 }, "d": 1 } }] }', '{ "d" : 1, "_id": 2.0 }');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1, "b": 2 }', '{ "": [ { "$unset": "b" }, { "$set": { "b": 1 } }] }', '{}');

-- simple case for replaceRoot
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1, "b": 2 }', '{ "": [ { "$replaceRoot" : { "newRoot": { "a_key" : "$a" }} }]}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 2, "a": { "b": {"arr" : [1, 2, 3, {}], "a" : {"b": 1}, "int" : 1} }, "b": 2 }', '{ "": [ { "$replaceRoot" : { "newRoot": { "a_b_key" : "$a.b" }} }]}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 2, "a": { "b": {"arr" : [1, 2, 3, {}], "a" : {"b": 1}, "int" : 1} }, "b": 2 }', '{ "": [ { "$replaceRoot" : { "newRoot": { "a_b_key" : "$a.b", "a_key" : "$a", "a_b_arr_key" : "$a.b.arr" }} }]}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 2, "a": { "b": {"arr" : [1, 2, 3, {}], "a" : {"b": 1}, "int" : 1} }, "b": 2 }', '{ "": [ { "$replaceRoot" : { "newRoot": "$a.b" }}]}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 2, "a": { "b": {"arr" : [1, 2, 3, {}], "a" : {"b": 1}, "int" : 1} }, "b": 2 }', '{ "": [ { "$replaceRoot" : { "newRoot":  {"$mergeObjects":  [ { "dogs": 0, "cats": 0, "birds": 0, "fish": 0 }, "$a.b" ] } }}]}', '{}');

-- simple case for replaceWith
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1, "b": 2 }', '{ "": [ { "$replaceWith" : { "newRoot": { "a_key" : "$a" }} }]}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 2, "a": { "b": {"arr" : [1, 2, 3, {}], "a" : {"b" : 1}, "int" : 1} }, "b": 2 }', '{ "": [ { "$replaceWith" : { "newRoot": { "a_b_key" : "$a.b" }} }]}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 2, "a": { "b": {"arr" : [1, 2, 3, {}], "a" : {"b": 1}, "int" : 1} }, "b": 2 }', '{ "": [ { "$replaceWith" : { "newRoot": { "a_b_key" : "$a.b", "a_key" : "$a", "a_b_arr_key" : "$a.b.arr" }} }]}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 2, "a": { "b": {"arr" : [1, 2, 3, {}], "a" : {"b": 1}, "int" : 1} }, "b": 2 }', '{ "": [ { "$replaceWith" : { "newRoot": "$a.b" }}]}', '{}');

-- update: Error cases:
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b": [1, 2, 3 ]} }', '{ "": { "$set": { "a.b.7": 9, "a.b.10": 13 }, "$unset": { "a.b.7": 1 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [ { "b": 2 } ] }', '{ "": { "$rename": { "a.0": "b" } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [ { "b": 2 } ] }', '{ "": { "$rename": { "a.0.b": "a.1.c" } } }', '{}');
SELECT helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "a": 2, "h": 6 } } }', '{ "_id": { "$eq": 1 }, "c": 3, "$and": [ { "c": 4 } ] }');
SELECT helio_api_internal.bson_update_document('{"a": [1], "b": {"c": [2]}, "d": [{"e": 3}], "f": 4}', '{ "": { "$rename" :{"f":"d.e"}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"a": [1], "b": {"c": [2]}, "d": [{"e": 3}], "f": 4}', '{ "": { "$rename" :{"f":"d.e"}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"a": [1], "b": {"c": [2]}, "d": [{"e": 3}], "f": 4}', '{ "": { "$rename" :{"d.e":"d.c"}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"a": [1], "b": {"c": [2]}, "d": [{"e": 3}], "f": 4}', '{ "": { "$rename" :{"f":"a.0"}} }', '{}');

-- $push top level input validation with all modifiers
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": {"$numberInt" : "2"}}', '{ "": { "$push" :{"a":"0"}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": {"$numberLong" : "2"}}', '{ "": { "$push" :{"a":"0"}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": {"$numberDouble" : "2.0"}}', '{ "": { "$push" :{"a":"0"}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": {"b" : [1,2,3]}}', '{ "": { "$push" :{"a":"0"}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": false}', '{ "": { "$push" :{"a":"0"}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": NaN}', '{ "": { "$push" :{"a":"0"}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": "6" }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": 6 }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": {"a": 6} }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$slice": 1.1 }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$slice": "2" }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$slice": true }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$slice": [1,2,3] }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$sort": 0 }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$sort": -2 }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$sort": 2 }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$sort": 1.1 }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$sort": {"a.b": -2} }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$sort": {"a.": -2} }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$sort": {".b.c": -2} }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$sort": {"": 1} }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$sort": {} }}} }', '{}');
-- TODO: Make the error compatible with mongo: "errmsg" : "Cannot create field 'y' in element {x: [ { y: 1 } ]}"
SELECT helio_api_internal.bson_update_document('{"_id": 1, "key": {"x": [{"y": [1]}, {"y": [2]}]}}', '{ "": { "$push" :{"key.x.y": 1 }}}', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "key": {"x": [{"y": [1]}, {"y": [2]}]}}', '{ "": { "$push" :{ "key.x": { "$eachh": [], "$position": 1, "$sort" : 1} }}}', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "key": {"x": [{"y": [1]}, {"y": [2]}]}}', '{ "": { "$push" :{ "key.x": { "$each": [], "badplugin": 1} }}}', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$position": -0.1 }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$position": 1.1 }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$position": "start" }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$position": false }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$position": {"a.b": -2} }}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$each": [], "$position": [1,2,3] }}} }', '{}');

-- $push without $each but with other modifiers works on mongo5.0, but here it errors out because $ is not supported in key names yet
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" :{"a": { "$position": 5, "$slice": 3, "$sort": -1 }}} }', '{}');

-- $push valid cases when modifiers are not persent
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" : {"a": 6} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$push" : {"a": {"another": 6}} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3]}', '{ "": { "$push" : {"a": false} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3]}', '{ "": { "$push" : {"a": [5,6,7]} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3]}', '{ "": { "$push" : {"a": 1.21} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "key": 1, "scores": [44,78,38,80], "a": {"scores": [1,2]}}', '{ "": { "$push" : {"scores": 1, "a.scores": 10} } }', '{}');

-- $push with $each modifier in non-dotted path
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3]}', '{ "": { "$push" : {"a": { "$each" : [1,2,"3", {"four": 4}] }} } }', '{}');

-- $push with array indices as dotted path
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3]}', '{ "": { "$push" : {"a.3": 4 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3]}', '{ "": { "$push" : {"a.7": 7 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [[1],[2],[3]]}', '{ "": { "$push" : {"a.0.5": 7 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [[1],[2],[3]]}', '{ "": { "$push" : {"a.0.5": { "$each": [10,9,8], "$sort" : 1 } } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [[1],[2],3]}', '{ "": { "$push" : {"a.2.5": 7 } } }', '{}');

-- $push with $each & $slice modifier in non-dotted path
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5,6,7,8,9,10]}', '{ "": { "$push" : {"a": { "$each" : [], "$slice": 5 }} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5,6,7,8,9,10]}', '{ "": { "$push" : {"a": { "$each" : [], "$slice": -6 }} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5,6,7,8,9,10]}', '{ "": { "$push" : {"a": { "$each" : [], "$slice": 20 }} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5,6,7,8,9,10]}', '{ "": { "$push" : {"a": { "$each" : [], "$slice": -11 }} } }', '{}');

-- $push with $each & $position modifier in non-dotted path
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5,6,7,8,9,10]}', '{ "": { "$push" : {"a": { "$each" : [-1, 0], "$position": 20 }} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5,6,7,8,9,10]}', '{ "": { "$push" : {"a": { "$each" : [-1, 0], "$position": -20 }} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5,6,7,8,9,10]}', '{ "": { "$push" : {"a": { "$each" : [-1, 0], "$position": 6 }} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5,6,7,8,9,10]}', '{ "": { "$push" : {"a": { "$each" : [-1, 0], "$position": -8 }} } }', '{}');

-- $push with $sort modifier in non-dotted path
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3]}', '{ "": { "$push" : {"a": { "$each" : [1,2,"3", {"four": 4}, false, [3,1,2]], "$sort": -1 }} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [{"wk": 1, "scores": 85}, {"wk": 3, "scores": 30}, {"wk": 2, "scores": 75}, {"wk": 5, "scores": 99}]}',
                                                '{ "": { "$push" : {"a": { "$each" : [{"wk": 2, "scores": 70}], "$sort": 1 }} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [{"wk": 1, "scores": 85}, {"wk": 3, "scores": 30}, {"wk": 2, "scores": 75}, {"wk": 5, "scores": 99}]}',
                                                '{ "": { "$push" : {"a": { "$each" : [{"wk": 2, "scores": 70}], "$sort": { "scores" : -1 } }} } }', '{}');
-- Sort spec dotted path contains intermediate array, sort spec path not found so same sort order
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1,"key": 1,"a": [{"b": [{"c": 5}]},{"b": [{"c": 10}]},{"b": [{"c": 3}]},{"b": [{"c": 2}]}]}',
                                                '{ "": { "$push" : {"a": { "$each" : [], "$sort": { "b.c" : 1 } }} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1,"key": 1,"a": [{"b": [{"c": 5}]},{"b": [{"c": 10}]},{"b": [{"c": 3}]},{"b": [{"c": 2}]}]}',
                                                '{ "": { "$push" : {"a": { "$each" : [], "$sort": { "b.c" : -1 } }} } }', '{}');
-- Sort spec dotted path doesn't exist, same sort order
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1,"key": 1,"a": [{"b": [{"c": 5}]},{"b": [{"c": 10}]},{"b": [{"c": 3}]},{"b": [{"c": 2}]}]}',
                                                '{ "": { "$push" : {"a": { "$each" : [], "$sort": { "c" : -1 } }} } }', '{}');
-- Sort spec with array and valid index element
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1,"key": 1,"a": [{"b": [{"c": 5}]},{"b": [{"c": 10}]},{"b": [{"c": 3}]},{"b": [{"c": 2}]}]}',
                                                '{ "": { "$push" : {"a": { "$each" : [], "$sort": { "b.0" : -1 } }} } }', '{}');
-- Multiple Sort spec with existing or non exisiting paths
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id":1,"key":1,"a":[{"a":5,"b":10},{"a":2,"b":2},{"a":8,"b":3},{"a":4,"b":1},{"a":2,"b":5},{"a":2,"b":1}]}',
                                                '{ "": { "$push" : {"a": { "$each" : [], "$sort": { "a" : 1, "b": -1 } }} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id":1,"key":1,"a":[{"a":5,"b":10},{"a":2,"b":2},{"a":8,"b":3},{"a":4,"b":1},{"a":2,"b":5},{"a":2,"b":1}]}',
                                                '{ "": { "$push" : {"a": { "$each" : [], "$sort": { "a" : 1, "b": -1, "c": 1 } }} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id":1,"key":1,"a":[{"a":5,"b":10},{"a":2,"b":2},{"a":8,"b":3},{"a":4,"b":1},{"a":2,"b":5},{"a":2,"b":1}]}',
                                                '{ "": { "$push" : {"a": { "$each" : [], "$sort": { "d" : 1, "e": -1, "f": 1 } }} } }', '{}');

-- Sort spec with only some elements having sort path, other elements treat missing path as NULL
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1,"key": 1,"a": [{"b": 1}, {"a": 10, "b": 0}, {"b": 5}, {"b": 3}]}',
                                                '{ "": { "$push" : {"a": { "$each" : [], "$sort": { "a" : 1 } }} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1,"key": 1,"a": [{"b": 1}, {"a": 10, "b": 0}, {"b": 5}, {"b": 3}]}',
                                                '{ "": { "$push" : {"a": { "$each" : [], "$sort": { "a" : -1 } }} } }', '{}');


-- $push with combinations of other modifiers e.g $slice, $sort
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "arr": [{"a": 1, "b": 10}, {"a": 2, "b": 8}, {"a": 3, "b": 5}, {"a": 4, "b": 6}]}',
                                                '{ "": { "$push" : {"arr": { "$each" : [{"a": 5, "b": 8}, {"a": 6, "b": 7}, {"a": 7, "b": 6}], "$sort": { "b" : -1 }, "$slice": 3 }} } }', '{}');

-- $push & modifiers with dotted paths
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": {"b": {"c": [1,2,3,4,5,6,7,8,9,10]}}}', '{"" : {"$push": {"a.b.c": 11}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": {"b": {"c": [1,2,3,4,5,6,7,8,9,10]}}}', '{"" : {"$push": {"a.b.c": {"$each": [11,12], "$slice": -6, "$sort": -1, "$position": 4}}} }', '{}');

-- $push with absent fields
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "scores": {"a": {"b": [1]}}, "key": 1}', '{"" : {"$push": {"scores.a.c": {"x": [{"z": 1}]}}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "scores": {"a": {"b": [1]}}, "key": 1}', '{"" : {"$push": {"scores.a.c": {"$each": [5,4,3,2,1], "$slice": 3, "$sort": 1}}} }', '{}');

-- $push with key having dots
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "scores": {"a": {"b": [1]}}, "key": 1}', '{"" : {"$push": {"scores.a.b": {"x": [{"z.a": 1}]}}} }', '{}');

-- $push with NaN and Infinity values
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [{"$numberDecimal": "100"}, {"$numberDouble": "NaN"}, {"$numberDecimal": "2"}, {"$numberDecimal": "NaN"}, {"$numberDecimal": "23"}]}', '{ "": { "$push" : {"a": { "$each": [], "$sort" : 1 } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [{"$numberDecimal": "Infinity"}, {"$numberDouble": "NaN"}, {"$numberDecimal": "332"}, {"$numberDecimal": "NaN"}, {"$numberDecimal": "23"}]}', '{ "": { "$push" : {"a": { "$each": [], "$sort" : -1 } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [{"$numberDecimal": "NaN"}, {"$numberDouble": "NaN"}, {"$numberDecimal": "Infinity"}, {"$numberDecimal": "23"}]}', '{ "": { "$push" : {"a": { "$each": [], "$sort" : 1 } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [{"$numberDecimal": "NaN"}, {"$numberDouble": "NaN"}, {"$numberDecimal": "Infinity"}, {"$numberDecimal": "23"}]}', '{ "": { "$push" : {"a": { "$each": [], "$sort" : -1 } } } }', '{}');

-- test conflicting update operator paths
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a": 1 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a.b": 1 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$inc": {"a": 1 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$inc": {"a.b": 1 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$inc": {"a.b.c": 1 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$inc": {"a.b.c.d.e": 1 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a.b.c.d.e": 1 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a": {"b.c": 1} } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a": {"b": 1} } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a": {"b.c.d": 1} } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a": {"b.k.d": 1} } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a": {"b.c.d.e": 1} } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a": {"b.c.d": { "e": 1 } } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a": {"b.c.d": { "e.f": 1 } } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a": {"b.c.d": { "k.f": 1 } } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a": {"b.c.d": { "f.k": 1 } } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a": {"b.c.d": { "e.f": {"g": 1} } } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a.b.c.d": { "e": 1 } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a.b.c": { "d.e": 1 } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b": {"c": { "d.e.f": "1"} } }, "$unset": {"a.b": { "c.d.e": 1 } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b.c.d.e": 1 }, "$unset": {"a.b": 1 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b.c.d.e": 1 }, "$unset": {"a.b.c.d.e.f": 1 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"data": "data"}', '{ "": { "$set": { "a.b.c.d.e": 1 }, "$inc": {"a.b": 1 } } }', '{}');

-- update scenario tests: $currentDate

-- -- Function that extracts date / timestamp value of a given key in a json obj, and returns the equivalent epoch in ms
CREATE OR REPLACE FUNCTION get_epochms_from_key(obj json, qkey text, is_date bool)
RETURNS numeric
AS $$
DECLARE
    k text;
    qarr text[];
    epoch_seconds numeric;
    nanoseconds_in_second numeric;
    epoch_milliseconds numeric;
    tmp bool;
BEGIN
    SELECT string_to_array(qkey, '.') into qarr;    -- convert the nested keys in keys array (e.g. "a.b.c" to [a,b,c] )
    FOREACH k IN ARRAY qarr
    LOOP
        IF obj->k IS NULL THEN
            RETURN 0;
        END IF;
        SELECT obj->k into obj;
    END LOOP;

    IF is_date THEN
        IF (obj->'$date' IS NULL) OR (obj->'$date'->'$numberLong' IS NULL) THEN
            RETURN 0;
        END IF;
        SELECT obj->'$date'->>'$numberLong' into epoch_milliseconds;
    ELSE
        IF (obj->'$timestamp' IS NULL) OR (obj ->'$timestamp'->'t' IS NULL) OR (obj->'$timestamp'->'i' IS NULL) THEN
            RETURN 0;
        END IF;
        SELECT obj->'$timestamp'->>'t' into epoch_seconds;
        SELECT obj->'$timestamp'->>'i' into nanoseconds_in_second;
        epoch_milliseconds := epoch_seconds * 1000 + ROUND(nanoseconds_in_second / (1000 * 1000));
    END IF;

    RETURN epoch_milliseconds;
END
$$
LANGUAGE plpgsql;

-- -- Function to test that on updating a doc with $currentDate, the date/timestamp value lies between the timestamp values of just before & after calling the update api
CREATE OR REPLACE FUNCTION test_update_currentDate(doc bson, updateSpec bson, querySpec bson, qDates text[] DEFAULT '{}'::text[], qTimestamps text[] DEFAULT '{}'::text[])
RETURNS text
AS $$
DECLARE
    obj             json;
    epoch_bgn       text;
    epoch_end       text;
    qKey            text;
    epoch_msec      numeric;
    bgn_epoch_sec   numeric;
    bgn_usec        numeric;
    bgn_epoch_msec  numeric;
    end_epoch_sec   numeric;
    end_usec        numeric;
    end_epoch_msec  numeric;
BEGIN
    SELECT extract(epoch from clock_timestamp()) into epoch_bgn;    -- get a time just before the $currentDate execution
    SELECT newDocument FROM helio_api_internal.bson_update_document(doc, updateSpec, querySpec) into obj;
    SELECT extract(epoch from clock_timestamp()) into epoch_end;    -- get a time just after the $currentDate execution

    SELECT split_part(epoch_bgn, '.', 1)::numeric into bgn_epoch_sec;
    SELECT split_part(epoch_bgn, '.', 2)::numeric into bgn_usec;
    bgn_epoch_msec := bgn_epoch_sec * 1000 + ROUND(bgn_usec / 1000);

    SELECT split_part(epoch_end, '.', 1)::numeric into end_epoch_sec;
    SELECT split_part(epoch_end, '.', 2)::numeric into end_usec;
    end_epoch_msec := end_epoch_sec * 1000 + ROUND(end_usec / 1000);

    IF array_length(qDates, 1) != 0 THEN
        FOREACH qKey IN ARRAY qDates
        LOOP
            SELECT get_epochms_from_key(obj, qKey, true) into epoch_msec;
            IF epoch_msec NOT BETWEEN bgn_epoch_msec AND end_epoch_msec THEN
                RETURN 'TEST FAILED : Stored Date for "' || qkey || '" may not be a valid value ' || epoch_msec;
            END IF;
        END LOOP;
    END IF;

    IF array_length(qTimestamps, 1) != 0 THEN
        FOREACH qKey IN ARRAY qTimestamps
        LOOP
            SELECT get_epochms_from_key(obj, qKey, false) into epoch_msec;
            IF epoch_msec NOT BETWEEN bgn_epoch_msec AND end_epoch_msec THEN
                RETURN 'TEST FAILED : Stored Timestamp for "' || qkey || '" may not be a valid value ' || epoch_msec;
            END IF;
        END LOOP;
    END IF;

    RETURN 'TEST PASSED';
END;
$$
LANGUAGE plpgsql;

-- -- tests to update existing field to current timestamp using { "$type" : "timestamp" }
SELECT test_update_currentDate('{"_id": 1, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', '{ "": { "$currentDate": { "a": { "$type" : "timestamp"} } } }', '{}', ARRAY[]::text[], ARRAY['a']::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": { "$date": { "$numberLong" : "1234567890000" }}}', '{ "": { "$currentDate": { "a": { "$type" : "timestamp"} } } }', '{}', ARRAY[]::text[], ARRAY['a']::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": "Hello" }', '{ "": { "$currentDate": { "a": { "$type" : "timestamp"} } } }', '{}', ARRAY[]::text[], ARRAY['a']::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": 2 }', '{ "": { "$currentDate": { "a": { "$type" : "timestamp"} } } }', '{}', ARRAY[]::text[], ARRAY['a']::text[]);

-- -- tests to update existing field to current date using { true }
SELECT test_update_currentDate('{"_id": 1, "a": { "$date": { "$numberLong" : "1234567890000" }}}', '{ "": { "$currentDate": { "a": true} } } ', '{}', ARRAY['a']::text[], ARRAY[]::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', '{ "": { "$currentDate": { "a": true} } } ', '{}', ARRAY['a']::text[], ARRAY[]::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": "Hello" }', '{ "": { "$currentDate": { "a": true} } } ', '{}', ARRAY['a']::text[], ARRAY[]::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": 2 }', '{ "": { "$currentDate": { "a": true} } } ', '{}', ARRAY['a']::text[], ARRAY[]::text[]);

-- -- tests to update existing field to current date using { false }
SELECT test_update_currentDate('{"_id": 1, "a": { "$date": { "$numberLong" : "1234567890000" }}}', '{ "": { "$currentDate": { "a": false} } } ', '{}', ARRAY['a']::text[], ARRAY[]::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', '{ "": { "$currentDate": { "a": false} } } ', '{}', ARRAY['a']::text[], ARRAY[]::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": "Hello" }', '{ "": { "$currentDate": { "a": false} } } ', '{}', ARRAY['a']::text[], ARRAY[]::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": 2 }', '{ "": { "$currentDate": { "a": false} } } ', '{}', ARRAY['a']::text[], ARRAY[]::text[]);

-- -- tests to update existing field to current date using { "$$type" : "date" }
-- -- Note: Due to a bug in libbson, parsing of { "$type" : "date" } fails. So temporarily the HelioApi $currentDate impl also supports {"$$type" : "date" }
-- -- Once the bug is fixed, these tests need to be updated from "$$type" to "$type"
SELECT test_update_currentDate('{"_id": 1, "a": { "$date": { "$numberLong" : "1234567890000" }}}', '{ "": { "$currentDate": { "a": { "$$type" : "date"} } } } ', '{}', ARRAY['a']::text[], ARRAY[]::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', '{ "": { "$currentDate": { "a": { "$$type" : "date"} } } } ', '{}', ARRAY['a']::text[], ARRAY[]::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": "Hello" }', '{ "": { "$currentDate": { "a": { "$$type" : "date"} } } } ', '{}', ARRAY['a']::text[], ARRAY[]::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": 2 }', '{ "": { "$currentDate": { "a": { "$$type" : "date"} } } } ', '{}', ARRAY['a']::text[], ARRAY[]::text[]);

-- -- tests to update existing field to timestamp, and add new fields with current date / timestamp
SELECT test_update_currentDate('{"_id": 1, "a": "Hello" }', '{ "": { "$currentDate": { "a": { "$type" : "timestamp"}, "b.c" : { "$type" : "timestamp"} } } } ', '{}', ARRAY[]::text[], ARRAY['a','b.c']::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": "Hello" }', '{ "": { "$currentDate": { "a": { "$type" : "timestamp"}, "b.c" : { "$$type" : "date"} } } } ', '{}', ARRAY['b.c']::text[], ARRAY['a']::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": "Hello" }', '{ "": { "$currentDate": { "a": { "$type" : "timestamp"}, "b.c" : true } } } ', '{}', ARRAY['b.c']::text[], ARRAY['a']::text[]);
SELECT test_update_currentDate('{"_id": 1, "a": "Hello" }', '{ "": { "$currentDate": { "a": { "$type" : "timestamp"}, "b.c" : false } } } ', '{}', ARRAY['b.c']::text[], ARRAY['a']::text[]);

-- -- these tests should give error messages

SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', '{ "": { "$currentDate": { "a": { } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', '{ "": { "$currentDate": { "a": {"$numberInt" : "1"} } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', '{ "": { "$currentDate": { "a": {"$numberLong" : "1"} } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', '{ "": { "$currentDate": { "a": {"$numberDouble" : "1.0"} } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', '{ "": { "$currentDate": { "a": "Hello" } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', '{ "": { "$currentDate": { "a": NaN } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', '{ "": { "$currentDate": { "a": { "$type" : "Hello" } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', '{ "": { "$currentDate": { "a": { "$$type" : true } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$timestamp": { "t": 1234567890, "i": 1 }}}', '{ "": { "$currentDate": { "a": { "$$type" : 2 } } } }', '{}');

SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$date": { "$numberLong" : "1234567890000" }}}', '{ "": { "$currentDate": { "a": { } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$date": { "$numberLong" : "1234567890000" }}}', '{ "": { "$currentDate": { "a": {"$numberInt" : "1"} } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$date": { "$numberLong" : "1234567890000" }}}', '{ "": { "$currentDate": { "a": {"$numberLong" : "1"} } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$date": { "$numberLong" : "1234567890000" }}}', '{ "": { "$currentDate": { "a": {"$numberDouble" : "1.0"} } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$date": { "$numberLong" : "1234567890000" }}}', '{ "": { "$currentDate": { "a": "Hello" } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$date": { "$numberLong" : "1234567890000" }}}', '{ "": { "$currentDate": { "a": NaN } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$date": { "$numberLong" : "1234567890000" }}}', '{ "": { "$currentDate": { "a": { "$type" : "Hello" } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$date": { "$numberLong" : "1234567890000" }}}', '{ "": { "$currentDate": { "a": { "$$type" : true } } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$date": { "$numberLong" : "1234567890000" }}}', '{ "": { "$currentDate": { "a": { "$$type" : 2 } } } }', '{}');

-- -- Note : This test intenionally provides { "$type" : "date" } instead of { "$$type" : "date" }
-- -- The API call returns Error due to a Bug in libbson library that fails to parse { "$type" : "date" }
-- -- If this test starts failing (i.e. api call does't return error), it may mean that the libbson bug has been fixed.
-- -- In that case $currentDate implementation in helioapi needs a fix as well (i.e. remove support for "$$type" since "$type" will work OK)
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "$date": { "$numberLong" : "1234567890000" }}}', '{ "": { "$currentDate": { "a": { "$type" : "date" } } } }', '{}');

-- --Empty UpdateSpec check
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$inc": { "": 11} } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$mul": { "a.": 11} } }', '{}');

-- update scenario tests: $pop
-- -- $pop from begin of array
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" :{"a": -1}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1]}', '{ "": { "$pop" :{"a": -1}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": []}', '{ "": { "$pop" :{"a": -1}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b" : { "c" : [1,2,3,4,5] }} }', '{ "": { "$pop" :{"a.b.c": -1}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5], "b" : [6,7,8,9] }', '{ "": { "$pop" :{"a": -1, "b": -1}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5], "b" : [6,7,8,9] }', '{ "": { "$pop" :{"a": -1, "b": -1, "c" : -1}} }', '{}');

-- -- $pop from end of array
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" :{"a": 1}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [5]}', '{ "": { "$pop" :{"a": 1}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": []}', '{ "": { "$pop" :{"a": 1}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "b" : { "c" : [1,2,3,4,5] }} }', '{ "": { "$pop" :{"a.b.c": 1}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5], "b" : [6,7,8,9] }', '{ "": { "$pop" :{"a": 1, "b": 1}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5], "b" : [6,7,8,9] }', '{ "": { "$pop" :{"a": 1, "b": 1, "c" : 1}} }', '{}');

-- -- $pop from begin and end of array
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5], "b" : [6,7,8,9]}', '{ "": { "$pop" :{"a": -1, "b": 1}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5], "b" : [6,7,8,9]}', '{ "": { "$pop" :{"z": 1, "a": -1, "b": 1}} }', '{}');
  -- For below test, the spec parsing func (ParseDotPathAndGetTreeNode) throws error, but mongoDB 5.0 doesn't. TODO : Fix ParseDotPathAndGetTreeNode
  -- SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" :{"a": -1, "a": 1}} }', '{}');

-- -- Empty $pop document. Should not pop anything
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : {}} }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": []}', '{ "": { "$pop" : {}} }', '{}');

-- -- Error Cases : $pop on Non-Array Fields
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b" : { "c" : {"$numberInt" : "2"} }} }', '{ "": { "$pop" :{"a.b.c": 1}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b" : { "c" : {"$numberLong" : "2"} }} }', '{ "": { "$pop" :{"a.b.c": 1}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b" : { "c" : {"$numberDouble" : "2.0"} }} }', '{ "": { "$pop" :{"a.b.c": 1}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b" : { "c" : "Hello" }} }', '{ "": { "$pop" :{"a.b.c": 1}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b" : { "c" : false }} }', '{ "": { "$pop" :{"a.b.c": 1}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b" : { "c" : NaN }} }', '{ "": { "$pop" :{"a.b.c": 1}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b" : { "c" : [5,6,7,8], "d" : "Hello" }} }', '{ "": { "$pop" :{"a.b.c": 1, "a.b.d": -1 }} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b" : { "c" : [5,6,7,8], "d" : "Hello" }} }', '{ "": { "$pop" :{"a" : {"b" : { "c": 1 ,  "d": -1} }} } }', '{}');

-- -- Error Cases : $pop's key's value is neither -1 nor 1
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : {"a": "Hello"}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : {"a": true}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : {"a": NaN}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : {"a": []}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : {"a": {}}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : {"a": {"$numberInt" : "2"}}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : {"a": {"$numberDouble" : "-1.5"}}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b" : { "c" : [5,6,7,8]}} }', '{ "": { "$pop" : {"a.b.c": 2 }} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b" : { "c" : [5,6,7,8], "d" : "Hello" }} }', '{ "": { "$pop" :{"a.b.c": 25, "a.b.d": -1 }} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": { "b" : { "c" : [5,6,7,8], "d" : "Hello" }} }', '{ "": { "$pop" :{"a.b.c": 1, "a.b.d": -25 }} }', '{}');

-- -- Error Cases : $pop is not a document
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : []} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : [{"a" : 1}]} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : "Hello"} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : false} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : NaN} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : {"$numberInt" : "1"}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : {"$numberLong" : "-1"}} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pop" : {"$numberDouble" : "2.5"}} }', '{}');
-- --Update the Field with Periods (.) and Dollar Signs ($)
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": { "$b": 2 } }', '{ "": { "$inc": { "a.$b": 2 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "$a": { "b": 2 } }', '{ "": { "$inc": { "$a.c": 2 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "$a": { "$b": { "c" :  2 } } }', '{ "": { "$inc": { "$a.$b.c": 1 } } }', '{}');

-- --Negative scenarios for field names with Dollar Signs ($)
SELECT helio_api_internal.bson_update_document('{"_id" : 1}', '{ "": { "$set": { "$a": 2 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{}', '{ "": { "_id" : 1, "$set": { "$a": 2 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id" : 1, "a" : 1 }', '{ "": { "_id" : 1, "$b" : 2} }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "$a": { "$b": { "c" :  2 } } }', '{ "": { "$set": { "$a": { "$b" : { "$d": 1 } } } } } ', '{}');

-- more scenarios for update field names with $ signs

-- $ paths not allowed when it's not an upsert
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": "1" }', '{ "": { "$set": { "$secret.agent.x": 1 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "$secret": { "agent": 1 } }', '{ "": { "$set": { "$secret.agent.x": 1 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "$secret": { "agent": { "y": 1 } } }', '{ "": { "$set": { "$secret.agent.x": 1 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{ "_id": 5 }', '{ "": { "$set": { "$inc": 5 } } }', '{}');

-- allowed on upsert.
SELECT helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "$secret.agent.x": 1, "_id": 6 } } }', '{}');
SELECT helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "$secret.agent.x": 1 } } }', '{"_id": 2 }');
SELECT helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "$secret.agent.x": 1 } } }', '{"a": 3, "_id": 4 }');
SELECT helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "$inc": 5 } } }', '{"_id": 5 }');

-- $pull invalid cases
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": "text"}', '{ "": { "$pull": { "a": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1}', '{ "": { "$pull": { "a": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": true}', '{ "": { "$pull": { "a": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": {"$numberDecimal": "1.23"}}', '{ "": { "$pull": { "a": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": {"b": 2}}', '{ "": { "$pull": { "a": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [{"b": 1, "c": 1}, {"b": 2, "c": 2}, {"b": 3, "c": 3}]}', '{ "": { "$pull": { "a.0": {"b": 2, "c":2} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [{"b": [1], "c": [1]}, {"b": [2], "c": [2]}, {"b": [3], "c": [3]}]}', '{ "": { "$pull": { "a.b": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pull": { "a": {"$lte": 2, "b": 2} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pull": { "a": {"b": 2, "$lte": 2} } } }', '{}');

-- valid $pull without expressions in arrays and array of arrays
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pull": { "a": 3 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [{"b": 1, "c": 1}, {"b": 2, "c": 2}, {"b": 3, "c": 3}]}', '{ "": { "$pull": { "a": {"b": 2, "c":2} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [[1,2,3,4,5], [6,7,8,9,10]]}', '{ "": { "$pull": { "a.0": 4 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [[{"b": 1, "c": 1}, {"b": 2, "c": 2}, {"b": 3, "c": 3}], [{"b": 4, "c": 4}, {"b": 5, "c": 5}, {"b": 6, "c": 6}]]}', '{ "": { "$pull": { "a.1": {"b": 4, "c": 4} } } }', '{}');

-- valid $pull with expressions in arrays and array of arrays
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5,6,7,8,9,10]}', '{ "": { "$pull": { "a": {"$gte": 2, "$lte": 5} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [[1],[2],[3],[4],[5],[6],[7],[8],[9],[10]]}', '{ "": { "$pull": { "a": {"$gte": 5} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5,6,7,8,9,10]}', '{ "": { "$pull": { "a": {"$gte": 5, "$ne": 8} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5,6,7,8,9,10]}', '{ "": { "$pull": { "a": {"$in": [3,6,9]} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5,6,7,8,9,10]}', '{ "": { "$pull": { "a": {"$nin": [3,6,9]} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5,6,7,8,9,10]}', '{ "": { "$pull": { "a": { "$not": {"$gt": 5 } } } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id":1,"fruits":[ "apples", "pears", "oranges", "grapes", "bananas" ],"vegetables":[ "carrots", "celery", "squash", "carrots" ]}',
                                                                                        '{"":{"$pull":{"fruits":{"$in":["apples","oranges"]},"vegetables":"carrots"}}}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id":2,"fruits":[ "plums", "kiwis", "oranges", "bananas", "apples" ],"vegetables":[ "broccoli", "zucchini", "carrots", "onions" ]}',
                                                                                        '{"":{"$pull":{"fruits":{"$in":["apples","oranges"]},"vegetables":"carrots"}}}', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [{ "item": "A", "score": 5 }, { "item": "B", "score": 8 }, { "item": "C", "score": 8 }, { "item": "B", "score": 4 }]}',
                                                                                        '{ "": { "$pull": { "a": { "score": 8 , "item": "B" } } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "results": [{ "item": "A", "score": 5, "answers": [ { "q": 1, "a": 4 }, { "q": 2, "a": 6 } ] }, { "item": "B", "score": 8, "answers": [ { "q": 1, "a": 8 }, { "q": 2, "a": 9 } ]}, { "item": "C", "score": 8, "answers": [ { "q": 1, "a": 8 }, { "q": 2, "a": 7 } ] }, { "item": "B", "score": 4, "answers": [ { "q": 1, "a": 0 }, { "q": 2, "a": 8 } ] }]}',
                                                                                        '{ "": { "$pull": { "results": { "answers": {"$elemMatch": { "q": 2, "a": { "$gte": 8 } } } } } } }', '{}');

-- First level of nested array is supported in native mongo only when pull spec value is expression
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [[1],[2],[3],[4],[5],[6],[7],[8],[9],[10]]}', '{ "": { "$pull": { "a": {"$gte": 5} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "arr": [[{"a": 1, "b" : 1}], [{"a": 2, "b": 2}], [{"a": 3, "b": 3}]]}', '{ "": { "$pull": { "arr":  {"$eq": {"a": 1, "b": 1} } } } }', '{}');

-- First level of nested array is not recursed if $pull spec value is plain value
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "arr": [[1],[2],[3],[4],[5],[6],[7],[8],[9],[10]]}', '{ "": { "$pull": { "a":  5 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "arr": [[{"a": 1, "b" : 1}], [{"a": 2, "b": 2}], [{"a": 3, "b": 3}]]}', '{ "": { "$pull": { "arr":  {"a": 1, "b": 1} } } }', '{}');

-- Second and more are not supported
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [[[1,2]], [[3,4]]]}', '{ "": { "$pull": { "a": {"$gte": 3} } } }', '{}');

-- $pull no ops for no match
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [{"b": 1, "c": 1}, {"b": 2, "c": 2}, {"b": 3, "c": 3}]}', '{ "": { "$pull": { "a": {"b": 2, "c":3} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pull": { "c": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4,5]}', '{ "": { "$pull": { "a": {"$gt": 5} } } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": [{ "item": "A", "score": 5 }, { "item": "B", "score": 8 }, { "item": "C", "score": 8 }, { "item": "B", "score": 4 }]}',
                                                                                        '{ "": { "$pull": { "a": { "$elemMatch": { "score": 8 , "item": "B" } } } } }', '{}');

-- validate _id
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$set": { "_id": {"$b": 2, "c":3} } } }', '{}');

-- replaceRoot/replaceWith with dotted paths.
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": [ { "$replaceWith": { "_id": 1, "a.b.c": 3 } } ] }', '{}');

--upsert when querySpec has $all
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {} }', '{"_id": 10, "x": { "$all" : {}}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {} }', '{"_id": 11, "x": { "$all" : [1]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"_id":12}}}','{"x": { "$all" : [2]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"_id":13}}}' ,'{"x": { "$all" : [[3]]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"_id":14}}}','{"x": { "$all" : [{"x":11}]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"x":15}}}','{"_id": { "$all" : [15]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"x":16}}}','{"_id": { "$all" : [{"x": 16}]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"x":17}}}','{"_id": { "$all" : [[13]]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"_id":18}}}','{"x": { "$all" : [1,2,3]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"x":19}}}','{"_id": { "$all" : [15,16,17]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {}}','{"_id": { "$all" : [[15]]}}');

--upsert when querySpec has $in
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {} }','{"_id": 21, "x": { "$in" : {}}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {} }','{"_id": 22, "x": { "$in" : [1]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"_id":23}}}','{"x": { "$in" : [3423]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"_id":24}}}','{"x": { "$in" : [[2]]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"_id":25}}}','{"x": { "$in" : [{"y":2}]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"_id":26}}}','{"x": { "$in" : [10,20,30]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"x":27}}}','{"_id": { "$in" : [25]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"x":28}}}','{"_id": { "$in" : [{"y":26}]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"x":29}}}','{"_id": { "$in" : [[26]]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"_id":30}}}','{"_id": { "$in" : [27]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"_id":31}}}','{"_id": { "$in" : [27,28,29]}}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {} }','{"_id": { "$in" : [[27]]}}');
SELECT bson_dollar_project(newDocument, '{ "_id": 0 }') as newDocument from  helio_api_internal.bson_update_document('{}', '{ "": {"$set":{"y":31}}}','{"_id": { "$in" : [27,28,29]}}');

-- $nor operator in query spec
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"_id": 1, "b": 3} }', '{"$nor": [ { "_id": { "$eq" : 2}}]}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"_id": 1, "b": 3} }', '{"$nor": [ { "a": { "$eq" : 2}}]}');

--miscellaneous tests with multiple fields in queySpec
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {} }','{"$and": [{"_id": 1}, {"_id": 1}]}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set": {"a": 1}} }','{"_id.x": 1 , "_id.y": 2}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set": {"a": 1}} }','{"$and": [{"x": 1}, {"x": 1}]}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set": {"a": 1}} }','{"x": 1, "x.x": 1}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": {"$set": {"a": 1}} }','{"x.x": 1, "x": 1}');

-- show that we never crash due to passing one of the arguments as NULL
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": 1}', '{"": {"a": 2}}', null);
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": 1}', null, '{"b": 1}');
SELECT helio_api_internal.bson_update_document(null, '{"": {"a": 1}}', '{"c": 1}');
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": 1}', '{"": {"a": 2}}', '{"b": 1}', null, true);
SELECT helio_api_internal.bson_update_document('{"_id": 1, "a": 1}', '{"": {"a": 2}}', '{"b": 1}', '{"a": 1}', true);

-- $rename Negative/No-Op cases
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$rename": { "a.": "b"} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$rename": { "$.": "b"} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$rename": { "$[].0": "b"} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$rename": { "a": "b."} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$rename": { "a": "$"} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{}', '{ "": { "$rename": { "a": "$[]"} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"a": 1, "f": 1}', '{ "": { "$rename": { "a": "f.g"} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"a": 1, "f": 1}', '{ "": { "$rename": { "f.g": "a"} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "a": 1, "f": 1}', '{ "": { "$rename": { "x": "f.g"} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"a" : 1, "f" : 1, "b" : { "c" : { "d" : 1 } }}', '{ "": { "$rename": { "b.c.d.f": "t.k"} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"key": 1,"key2": 2,"f": {"g": 1, "h": 1},"j": 1,"k": 1}', '{ "": { "$rename": { "f.g": "k.m","f.h":"j.m"} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"key": 1,"key2": 2,"f": {"g": 1, "h": 1},"j": {},"k": 1}', '{ "": { "$rename": { "f.g": "k.m","f.h":"j.m"} } }', '{}');

--$rename working complex cases
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 1, "key": 1,"key2": 2,"f": {"g": 1, "h": 1},"h":1}', '{ "": { "$rename": { "key": "f.g"} } }', '{}');
SELECT newDocument as bson_update_document FROM helio_api_internal.bson_update_document('{"_id": 2, "key": 2,"x": {"y": 1, "z": 2}}', '{ "": { "$rename": { "key": "newName","x.y":"z","x.z":"k"} } }', '{}');