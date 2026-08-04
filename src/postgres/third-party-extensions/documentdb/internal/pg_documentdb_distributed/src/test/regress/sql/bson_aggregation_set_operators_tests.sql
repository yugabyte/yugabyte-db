SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 9000000;
SET documentdb.next_collection_id TO 9000;
SET documentdb.next_collection_index_id TO 9000;

-- $setIntersection operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[1,2,3],[2,3,4]]} }');
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [["a","b","c"],["a","d","c"]]} }');
select bson_dollar_project('{"_id":"1"}', '{"a" : { "$setIntersection" : [[1.1,2.2,3.3],[1.1,2.2,4.4]]} }');

-- $setIntersection operator: No matching element:
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[1,2,3],[4,5,6]]} }');
select bson_dollar_project('{"_id":"1"}', '{"a" : { "$setIntersection" : [[1,1,2],[3,3,4]]} }');

-- $setIntersection operator: Nested Elements:
-- NOTE: we don't recurse into arrays and considers only the first/outer-level array
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[[1,{"a":1, "b":1}],2,3],[4,[1,{"a":1, "b":1}],6]]} }');
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[{"a":[1,2,3],"b":1},2,3],[4,{"a":[1,2,3],"b":1},6]]} }');
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[[1,2,3]],[[3,2,1]]]} }');
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[{"a":1,"b":1}],[{"b":1,"a":1}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[["a","b","c"]],[["a","b","c"]]]} }');

---- $setIntersection operator: decimal,double and long:
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[1.1,2.1,3.1],[2.10,3.10,4.0]]} }');
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[1.1,2.1,3.1],[2.10,3.10,4.0]]} }');

---- $setIntersection operator: same value with different type:
-- if type is different is value is not fixed integer then it is not considering that as a match, e.g, double's 1.1 is not equals to decimal's 1.1
-- if type is different and values can be converted to fixed integer  then only it is considering as a match, e.g, double's 1.0 is equals to decimal's 1.0 
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[ { "$numberLong" : "1" },2,3],[1,2,3]]} }');
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[ { "$numberDecimal" : "1" },2,3],[1,2,3]]} }');
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[ 1,2,3],[{ "$numberDecimal" : "1" },2,3]]} }');
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[ 1.1],[{ "$numberDecimal" : "1.1" }]]} }'); -- 1.1 is not fixed integer and type is different so not considering both as equal.
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[ 1.00],[{ "$numberDecimal" : "1.00" }]]} }');
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[1],[{ "$numberDecimal" : "1.00" }]]} }');
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[1],[1.00]]} }');

---- $setIntersection operator: Null,Nan,Infinity,undefined and undefined path:
select bson_dollar_project('{"_id":"1" }', '{"setIntersection" : { "$setIntersection" : [null]} }');
select bson_dollar_project('{"_id":"1" }', '{"setIntersection" : { "$setIntersection" : ["$a"]} }');
select bson_dollar_project('{"_id":"1"}', '{"setIntersection" : { "$setIntersection" : null} }');
select bson_dollar_project('{"_id":"1"}', '{"setIntersection" : { "$setIntersection" : [[1,2,3],null]} }');
select bson_dollar_project('{"_id":"1"}', '{"setIntersection" : { "$setIntersection" : [{ "$undefined" : true }]} }');
select bson_dollar_project('{"_id":"1"}', '{"setIntersection" : { "$setIntersection" : { "$undefined" : true }} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"setIntersection" : { "$setIntersection" : ["$undefinePath", [1]] } }');
select bson_dollar_project('{"_id":"1"}', '{"intersection" : { "$setIntersection" : [[null, { "$numberDouble" : "Infinity"},{ "$numberDouble" : "-Infinity"},{ "$numberDouble" : "NaN"},{ "$numberDouble" : "-NaN"}],[null,{ "$numberDouble" : "Infinity"},{ "$numberDouble" : "-Infinity"},{ "$numberDouble" : "-NaN"}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setIntersection" : { "$setIntersection" : [[NaN],[{"$numberDouble": "-NaN"}]]} }');

---- $setIntersection operator: literals and operators:
select bson_dollar_project('{"_id":"1", "a" :[1,2,3]}', '{"intersection" : { "$setIntersection" : ["$a",[2,3,4]]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [ {"$slice" : [[1,2,3,4,5,6,7],2,5]} ,[2,3,4]]} }');
select bson_dollar_project('{"_id":"1","a":[1,2,3] }', '{"intersection" : { "$setIntersection" : [ {"$slice" : ["$a",0,3]} ,[2,3,4]]} }');

---- $setIntersection operator: Other BsonType Binary, code, DateTime, Timestamp, Minkey, Maxkey:
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [ [{"$binary": { "base64": "ww==", "subType": "01"}}], [{"$binary": { "base64": "zg==", "subType": "01"}}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [ [{"$binary": { "base64": "ww==", "subType": "01"}}], [{"$binary": { "base64": "ww==", "subType": "01"}}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [ [{"$binary": { "base64": "ww==", "subType": "01"}}], [{"$binary": { "base64": "ww==", "subType": "02"}}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{"$timestamp" : { "t": 1670981326, "i": 1 } }], [{"$timestamp" : { "t": 1670981326, "i": 1 } }]]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{"$timestamp" : { "t": 1770981326, "i": 1 } }], [{"$timestamp" : { "t": 1870981326, "i": 1 } }]]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{"$timestamp" : { "t": 1670981326, "i": 1 } }], [{"$timestamp" : { "t": 1670981326, "i": 2 } }]]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{"$date": "2013-12-12T06:23:15.134Z"}], [{"$date": "2013-12-12T06:23:15.134Z"}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{"$date": "2013-12-12T06:23:15.134Z"}], [{"$date": "2013-12-12T06:23:15.135Z"}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{"$oid": "639926cee6bda3127f153bf1"}],[{"$oid": "639926cee6bda3127f153bf1"}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{"$oid": "639926cee6bda3127f153bf1"}],[{"$oid": "739926cee6bda3127f153bf1"}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{ "$code": "var a = 1;"}],[{ "$code": "var a = 1;"}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{ "$code": "var a = 1;"}],[{ "$code": "var b = 1;"}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}],[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}],[{ "$dbPointer" : { "$ref" : "db.test2", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}],[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "447f000000c1de008ec19ceb" }}}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{"$maxKey" : 1}],[{"$maxKey" : 1}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{"$minKey" : 1}],[{"$minKey" : 1}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"intersection" : { "$setIntersection" : [[{"$minKey" : 1}],[{"$maxKey" : 1}]]} }');

---- $setIntersection operator: Negative:
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"intersection" : { "$setIntersection" : {} } }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"intersection" : { "$setIntersection" : 1 } }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"intersection" : { "$setIntersection" : [1] } }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"intersection" : { "$setIntersection" : [[1],{}]} }');

-- $setUnion operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[1,2,3],[2,3,4]]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [["a","b","c"],["a","d","c"]]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[1.1,2.2,3.3],[1.1,2.2,4.4]]} }');

-- $setUnion operator: No matching element:
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[1,2,3],[4,5,6]]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[1,1,2],[3,3,4]]} }');

-- $setUnion operator: Nested Elements:
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[[1,{"a":1, "b":1}],2,3],[4,[1,{"a":1, "b":1}],6]]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[{"a":[1,2,3],"b":1},2,3],[4,{"a":[1,2,3],"b":1},6]]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[[1,2,3]],[[3,2,1]]]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[{"a":1,"b":1}],[{"b":1,"a":1}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[["a","b","c"]],[["a","b","c"]]]} }');

---- $setUnion operator: decimal,double and long:
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[1.1,2.1,3.1],[2.10,3.10,4.0]]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[1.1,2.1,3.1],[2.10,3.10,4.0]]} }');

---- $setUnion operator: same value with different type:
-- if type is different is value is not fixed integer then it is not considering that as a match, e.g, double's 1.1 is not equals to decimal's 1.1
-- if type is different and values can be converted to fixed integer  then only it is considering as a match, e.g, double's 1.0 is equals to decimal's 1.0
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[ { "$numberLong" : "1" },2,3],[1,2,3]]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[ { "$numberDecimal" : "1" },2,3],[1,2,3]]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[ 1,2,3],[{ "$numberDecimal" : "1" },2,3]]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[ 1.1],[{ "$numberDecimal" : "1.1" }]]} }'); -- 1.1 is not fixed integer and type is different so not considering both as equal.
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[ 1.00],[{ "$numberDecimal" : "1.00" }]]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[1],[{ "$numberDecimal" : "1.00" }]]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[1],[1.00]]} }');

---- $setUnion operator: Null,Nan,Infinity,undefined and undefined path:
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [null]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : ["$a"]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : null} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[1,2,3],null]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [{ "$undefined" : true }]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : { "$undefined" : true }} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"setUnion" : { "$setUnion" : ["$undefinePath", [1]] } }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[null, { "$numberDouble" : "Infinity"},{ "$numberDouble" : "-Infinity"},{ "$numberDouble" : "NaN"},{ "$numberDouble" : "-NaN"}],[null,{ "$numberDouble" : "Infinity"},{ "$numberDouble" : "-Infinity"},{ "$numberDouble" : "-NaN"}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"union" : { "$setUnion" : [[NaN],[{"$numberDouble": "-NaN"}]]} }');

---- $setUnion operator: literals and operators:
select bson_dollar_project('{"_id":"1", "a" :[1,2,3]}', '{"union" : { "$setUnion" : ["$a",[2,3,4]]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [ {"$slice" : [[1,2,3,4,5,6,7],2,5]} ,[2,3,4]]} }');
select bson_dollar_project('{"_id":"1","a":[1,2,3] }', '{"union" : { "$setUnion" : [ {"$slice" : ["$a",0,3]} ,[2,3,4]]} }');

---- $setUnion operator: Other BsonType Binary, code, DateTime, Timestamp, Minkey, Maxkey:
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [ [{"$binary": { "base64": "ww==", "subType": "01"}}], [{"$binary": { "base64": "zg==", "subType": "01"}}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [ [{"$binary": { "base64": "ww==", "subType": "01"}}], [{"$binary": { "base64": "ww==", "subType": "01"}}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [ [{"$binary": { "base64": "ww==", "subType": "01"}}], [{"$binary": { "base64": "ww==", "subType": "02"}}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{"$timestamp" : { "t": 1670981326, "i": 1 } }], [{"$timestamp" : { "t": 1670981326, "i": 1 } }]]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{"$timestamp" : { "t": 1770981326, "i": 1 } }], [{"$timestamp" : { "t": 1870981326, "i": 1 } }]]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{"$timestamp" : { "t": 1670981326, "i": 1 } }], [{"$timestamp" : { "t": 1670981326, "i": 2 } }]]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{"$date": "2013-12-12T06:23:15.134Z"}], [{"$date": "2013-12-12T06:23:15.134Z"}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{"$date": "2013-12-12T06:23:15.134Z"}], [{"$date": "2013-12-12T06:23:15.134Z"}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{"$oid": "639926cee6bda3127f153bf1"}],[{"$oid": "639926cee6bda3127f153bf1"}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{"$oid": "639926cee6bda3127f153bf1"}],[{"$oid": "739926cee6bda3127f153bf1"}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{ "$code": "var a = 1;"}],[{ "$code": "var a = 1;"}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{ "$code": "var a = 1;"}],[{ "$code": "var b = 1;"}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}],[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}],[{ "$dbPointer" : { "$ref" : "db.test2", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}],[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "447f000000c1de008ec19ceb" }}}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{"$maxKey" : 1}],[{"$maxKey" : 1}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{"$minKey" : 1}],[{"$minKey" : 1}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"union" : { "$setUnion" : [[{"$minKey" : 1}],[{"$maxKey" : 1}]]} }');

---- $setUnion operator: Negative:
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"union" : { "$setUnion" : {} } }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"union" : { "$setUnion" : 1 } }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"union" : { "$setUnion" : [1] } }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5]  }', '{"union" : { "$setUnion" : [[1],{}]} }');

-- $setEquals operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[],[]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[1,2,3],[1,2,3]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [["a","b","c"],["a","b","c"]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[1.1,2.1,3.1],[1.1,2.1,3.1]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[1,2,3],[1,2,4]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [["a","b","c"],["b","b","c"]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[1.1,2.1,3.1],[1.2,2.2,3.2]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[],[],[]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[1,2,3],[1,2,3,4],[1,2,3,4,5]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[1,2,3],[1,2,3],[1,2,3],[1,2,3],[1,2,3]]} }');

-- $setEquals operator: Nested test:
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[[1,2],[1,2]],[[1,2],[1,2]]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[[1,2],[1,2]],[[2,3],[1,2]]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[1,2,1,2],[[2,3],[1,2]]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[{"a":1},{"b":1}],[{"a":1},{"b":1}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[{"a":1},{"b":1}],[{"a":1},{"b":2}]]} }');

-- $setEquals operator: same value with different type:
-- if type is different is value is not fixed integer then it is not considering that as a match, e.g, double's 1.1 is not equals to decimal's 1.1
-- if type is different and values can be converted to fixed integer  then only it is considering as a match, e.g, double's 1.0 is equals to decimal's 1.0 
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[{ "$numberLong" : "1" }],[1]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[{ "$numberLong" : "1" },{ "$numberDouble" : "1.0" }],[1],[{ "$numberDecimal" : "1.0" }]]} }');
select bson_dollar_project('{"_id":"1"}', '{"setEqual" : { "$setEquals" : [[{ "$numberDouble" : "1.1" }],[1.1],[{ "$numberDecimal" : "1.1" }]]} }');

--$setEquals with operators and literals
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5], "b" : [1,2,3,4,5]  }', '{"setEqual" : { "$setEquals" : [["$a"],["$b"]] } }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5], "b" : [1,2,3,4]  }', '{"setEqual" : { "$setEquals" : [["$a"],["$b"]] } }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5], "b" : [1,2,3,4]  }', '{"setEqual" : { "$setEquals" : [[{"$slice": ["$a",1,3]}],[{"$slice": ["$b",1,3]}]] } }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5], "b" : [1,2,3,4]  }', '{"setEqual" : { "$setEquals" : [[{"$slice": ["$a",1,4]}],[{"$slice": ["$b",1,3]}]] } }');

---- $setEquals operator: Other BsonType Binary, code, DateTime, Timestamp, Minkey, Maxkey:
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [ [{"$binary": { "base64": "ww==", "subType": "01"}}], [{"$binary": { "base64": "zg==", "subType": "01"}}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [ [{"$binary": { "base64": "ww==", "subType": "01"}}], [{"$binary": { "base64": "ww==", "subType": "01"}}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [ [{"$binary": { "base64": "ww==", "subType": "01"}}], [{"$binary": { "base64": "ww==", "subType": "02"}}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{"$timestamp" : { "t": 1670981326, "i": 1 } }], [{"$timestamp" : { "t": 1670981326, "i": 1 } }]]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{"$timestamp" : { "t": 1770981326, "i": 1 } }], [{"$timestamp" : { "t": 1870981326, "i": 1 } }]]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{"$timestamp" : { "t": 1670981326, "i": 1 } }], [{"$timestamp" : { "t": 1670981326, "i": 2 } }]]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{"$date": "2013-12-12T06:23:15.134Z"}], [{"$date": "2013-12-12T06:23:15.134Z"}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{"$date": "2013-12-12T06:23:15.134Z"}], [{"$date": "2013-12-12T06:23:15.134Z"}] ]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{"$oid": "639926cee6bda3127f153bf1"}],[{"$oid": "639926cee6bda3127f153bf1"}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{"$oid": "639926cee6bda3127f153bf1"}],[{"$oid": "739926cee6bda3127f153bf1"}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{ "$code": "var a = 1;"}],[{ "$code": "var a = 1;"}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{ "$code": "var a = 1;"}],[{ "$code": "var b = 1;"}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}],[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}],[{ "$dbPointer" : { "$ref" : "db.test2", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "347f000000c1de008ec19ceb" }}}],[{ "$dbPointer" : { "$ref" : "db.test", "$id" : { "$oid" : "447f000000c1de008ec19ceb" }}}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{"$maxKey" : 1}],[{"$maxKey" : 1}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{"$minKey" : 1}],[{"$minKey" : 1}]]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[{"$minKey" : 1}],[{"$maxKey" : 1}]]} }');

-- $setEquals operator: Negative test:
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : {} } }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : 1 } }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [1] } }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : ["$a",[1,2,3]] } }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[1,2,3],"$a"] } }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[1],{}]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : null} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : { "$undefined" : true}} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [null]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [null,null]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [[1,2,3],null]} }');
select bson_dollar_project('{"_id":"1" }', '{"setEqual" : { "$setEquals" : [{ "$undefined" : true}]} }');


-- $setDifference operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[],[]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[1,2,3],[]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[],[1,2,3]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[1,2],[1,2]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[1,1,2,2,3,3],[1,2]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[1,2,3,4],[1,2]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[1,2,3,4],[1,2,5,6,7]]} }');

---- $setDifference operator: same value with different type:
-- if type is different is value is not fixed integer then it is not considering that as a match, e.g, double's 1.1 is not equals to decimal's 1.1
-- if type is different and values can be converted to fixed integer  then only it is considering as a match, e.g, double's 1.0 is equals to decimal's 1.0 
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[{"$numberDecimal" :  "1.0"},{"$numberDouble" :  "2.0"},3,4],[1,2]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[{"$numberDecimal" :  "1.1"},{"$numberDecimal" :  "2.1"},3,4],[1.1,2.1]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[{"$numberDouble" :  "1.1"},{"$numberDecimal" :  "2.1"},3,4],[{"$numberDouble" :  "1.1"},{"$numberDecimal" :  "2.1"}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[{"$numberLong" :  "1"},{"$numberDouble" :  "2.0"},3,4],[1,2]]} }');

-- $setDifference operator: different types and Nested case:
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [["a","b","c"],["a"]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[null, { "$numberDouble" : "Infinity"},{ "$numberDouble" : "-Infinity"},{ "$numberDouble" : "NaN"},{ "$numberDouble" : "-NaN"}],[]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[null, { "$numberDouble" : "Infinity"},{ "$numberDouble" : "-Infinity"},{ "$numberDouble" : "NaN"}],[{ "$numberDouble" : "-NaN"}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[{"$maxKey" : 1}],[{"$maxKey" : 1}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[{"$minKey" : 1}],[{"$minKey" : 1}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[{"$minKey" : 1}],[{"$maxKey" : 1}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[{"$date": "2013-12-12T06:23:15.134Z"}], [{"$date": "2019-01-30T07:30:10.137Z"}] ]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[[1,2],[1,2,3]],[[1,2]]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[{"a":1},{"b":1}],[{"a":1},{"b":2}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [null,null]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[1,2,3],null]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [null,[1,2,3]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : ["$a",[1,2,3]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [{"$undefined": true},[1,2,3]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[1,2,3],{"$undefined": true}]} }');

-- $setDifference operator: with Inner Operators :
select bson_dollar_project('{"_id":"1", "a": [1,2,3], "b": [1,2]}', '{"difference" : { "$setDifference" : ["$a","$b"]} }');
select bson_dollar_project('{"_id":"1", "a": [1,2,3,5], "b": [1,2,3,4]}', '{"difference" : { "$setDifference" : ["$a","$b"]} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5], "b" : [1,2,3,4]  }', '{"difference" : { "$setDifference" : [{"$slice": ["$a",1,6]},{"$slice": ["$b",1,6]}] } }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5], "b" : [1,2,3,4]  }', '{"difference" : { "$setDifference" : [{"$slice": ["$a",1,3]},{"$slice": ["$b",1,3]}] } }');

-- $setDifference operator: Negative Cases :
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[],[],[]]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [1,2,3]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : []} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [1]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [null]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [{"$undefined": true}]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : null} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [1,2]} }');
select bson_dollar_project('{"_id":"1"}', '{"difference" : { "$setDifference" : [[1],2]} }');


-- $setIsSubset operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[],[]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[1,2,3],[]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[],[1,2,3]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[1,2],[1,2,3,4]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[1,1,2,2,3,3],[1,2,3,4,3]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[1,2,3,4],[1,2]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[1,2,3,4],[1,2,5,6,7]]} }');

---- $setIsSubset operator: same value with different type:
-- if type is different is value is not fixed integer then it is not considering that as a match, e.g, double's 1.1 is not equals to decimal's 1.1
-- if type is different and values can be converted to fixed integer  then only it is considering as a match, e.g, double's 1.0 is equals to decimal's 1.0 
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[{"$numberDecimal" :  "1.0"},{"$numberDouble" :  "2.0"},3,4],[1,2,4,3]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[{"$numberDecimal" :  "1.1"},{"$numberDecimal" :  "2.1"},3,4],[1.1,2.1,3,4,5]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[{"$numberDouble" :  "1.1"},{"$numberDecimal" :  "2.1"},3,4],[{"$numberDouble" :  "1.1"},{"$numberDecimal" :  "2.1"},3,4,5]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[{"$numberLong" :  "1"},{"$numberDouble" :  "2.0"}],[1,2,3,4]]} }');

-- $setIsSubset operator: different types and Nested case:
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [["a","b","c"],["a","b","c","d"]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[null, { "$numberDouble" : "Infinity"},{ "$numberDouble" : "-Infinity"}],[null, { "$numberDouble" : "Infinity"},{ "$numberDouble" : "-Infinity"},{ "$numberDouble" : "NaN"},{ "$numberDouble" : "-NaN"}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[{ "$numberDouble" : "-NaN"}],[null, { "$numberDouble" : "Infinity"},{ "$numberDouble" : "-Infinity"},{ "$numberDouble" : "NaN"}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[{"$maxKey" : 1}],[{"$maxKey" : 1}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[{"$minKey" : 1}],[{"$minKey" : 1}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[{"$minKey" : 1}],[{"$maxKey" : 1}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[{"$date": "2013-12-12T06:23:15.134Z"}], [{"$date": "2013-12-12T06:23:15.134Z"}] ]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[[1,2],[1,2,3]],[[1,2],[1,2,3],[1,2,3,4]]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[{"a":1},{"b":1}],[{"a":1},{"b":1},{"c":2}]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[{"a":1},{"b":2}],[{"a":1},{"b":1},{"c":2}]]} }');

-- $setIsSubset operator: with Inner Operators :
select bson_dollar_project('{"_id":"1", "a": [1,2,3], "b": [1,2,3,4]}', '{"isSubset" : { "$setIsSubset" : ["$a","$b"]} }');
select bson_dollar_project('{"_id":"1", "a": [1,2,3,5], "b": [1,2,3,4]}', '{"isSubset" : { "$setIsSubset" : ["$a","$b"]} }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5], "b" : [1,2,3,4,5,6]  }', '{"isSubset" : { "$setIsSubset" : [{"$slice": ["$a",1,6]},{"$slice": ["$b",1,6]}] } }');
select bson_dollar_project('{"_id":"1", "a" : [1,2,3,4,5], "b" : [1,2,3,4]  }', '{"isSubset" : { "$setIsSubset" : [{"$slice": ["$a",1,3]},{"$slice": ["$b",1,3]}] } }');

-- $setIsSubset operator: Negative Cases :
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[],[],[]]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [1,2,3]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : []} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [1]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [null]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [1,2]} }');
select bson_dollar_project('{"_id":"1"}', '{"isSubset" : { "$setIsSubset" : [[1],2]} }');
select bson_dollar_project('{"_id":"1" }', '{"isSubset" : { "$setIsSubset" : null} }');
select bson_dollar_project('{"_id":"1" }', '{"isSubset" : { "$setIsSubset" : [[1,2,3],null]} }');
select bson_dollar_project('{"_id":"1" }', '{"isSubset" : { "$setIsSubset" : [[1,2,3],"$a"]} }');
select bson_dollar_project('{"_id":"1" }', '{"isSubset" : { "$setIsSubset" : ["$a",[1,2,3]]} }');
select bson_dollar_project('{"_id":"1" }', '{"isSubset" : { "$setIsSubset" : { "$undefined" : true}} }');


--$allElementsTrue : Test for True Outputs
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[true]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[true,true]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[true, 1,"app", 11.2, {"$numberLong":"10"}, {"$numberDouble":"10"}, {"$numberDecimal":"10"}, [1,2,3], {"X": "Y"}]]}}');
SELECT * FROM bson_dollar_project('{"a" : 1, "b" : "test"}', '{"result": { "$allElementsTrue": [["$a", "$b"]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[true, 1, {"$numberDouble" : "0.1"}, 10]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[true, 1, {"$numberDecimal" : "0.1"}, 10]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[-1, -2.9, {"$numberDecimal" : "0.1"}, 10]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[1, {"$add": [0,1]}]]}}');

--$allElementsTrue : Test for False Outputs
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[false]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[true,false]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[true, 1, 0, 10]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[true, 1, null, 10]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[true, 1, "$b", 10]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[true, 1, {"$undefined" : true}, 10]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[true, 1, {"$numberDouble" : "0.0"}, 10]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[true, 1, {"$numberDecimal" : "0.0"}, 10]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[true, 1, {"$numberLong" : "0"}, 10]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[false, 0, 0, 1]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[1, {"$add": [0,0]}]]}}');

--$allElementsTrue : Negative Test
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": ["a"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [{"$undefined" : true}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[1,2,3],[3,4,5]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [[1,2,3],1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": [1, [1,2,3]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$allElementsTrue": ["$b"]}}');


--$anyElementTrue : Test for True Outputs
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[true]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[true,false]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[0,true,false]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[0,false,null,"$b",{"$undefined": true}, true]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[true, 1,"app", 11.2, {"$numberLong":"10"}, {"$numberDouble":"10"}, {"$numberDecimal":"10"}, [1,2,3], {"X": "Y"}]]}}');
SELECT * FROM bson_dollar_project('{"a" : 1, "b" : "test"}', '{"result": { "$anyElementTrue": [["$a", "$b"]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[true, 1, {"$numberDouble" : "0.1"}, 10]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[true, 1, {"$numberDecimal" : "0.1"}, 10]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[false, 1, {"$undefined" : true}, 10]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[false, null, {"$numberDouble" : "0.1"}, 0]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[false, "$b", {"$numberDecimal" : "1.0"}, 0]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[false, {"$numberLong" : "1"}, 0]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[0, {"$add": [0,1]}]]}}');

--$anyElementTrue : Test for False Outputs
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[false]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[false,false]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[0,false,null,"$b",{"$undefined": true}, {"$numberDecimal" : "0.0"}, {"$numberDouble" : "0.0"}, {"$numberLong" : "0"}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[false, 0, {"$undefined" : true}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[false, 0, {"$numberDouble" : "0.0"}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[false,{"$numberDecimal" : "0.0"}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[false, {"$numberLong" : "0"}]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[false, 0, null]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[0, {"$add": [0,0]}]]}}');

--$anyElementTrue : Negative Test
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": ["a"]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [null]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [{"$undefined" : true}]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[1,2,3],[3,4,5]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [[1,2,3],1]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": [1, [1,2,3]]}}');
SELECT * FROM bson_dollar_project('{}', '{"result": { "$anyElementTrue": ["$b"]}}');

