SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 2300000;
SET documentdb.next_collection_id TO 23000;
SET documentdb.next_collection_index_id TO 23000;

--Test Set1 : array of document Tests
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1,"a" : { "b" : [ { "c" : 1, "d" : 2 }, { "c" : 5, "d" : 6 } ], "H" : { "c" : 10 } }},','{ "": { "$set": { "a.b.$[].c": 11 } } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [{"c":5}] }', '{ "": { "$set": { "a.$[].c": 30 } } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1,"a" : { "b" : [ { "x":[1,2,3,4] }, { "y": [1,2,3,4] } ]  }}','{ "": { "$inc": { "a.b.0.x.$[]": 100 } } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1,"a" : { "b" : [ { "x":[1,2,3,4] }, { "x": [1,2,3,4] } ]  }}','{ "": { "$inc": { "a.b.$[].x.0": 100 } } }', '{}');


-- Test Set2 : Elements in updatespec is not present in Document
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1,"a" : { "b" : [ { "c" : 1, "d" : 2 }, { "d" : 6 } ], "H" : { "c" : 10 } }},','{ "": { "$set": { "a.b.$[].c": 11 } } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1,"a" : { "b" : [ { "d" : 2 }, { "d" : 6 } ] }},','{ "": { "$set": { "a.b.$[].c": 11 } } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1,"a" : { "b" : [ { "x":[1,2,3,4] }, { "y": [1,2,3,4] } ]  }}','{ "": { "$set": { "a.b.$[].c": 11 } } }', '{}');

--Test Set3 : $set Operator
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$set": { "a.$[]": 30 } } }', '{}');

--Test Set4 : $inc Operator
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$inc": { "a.$[]": 30 } } }', '{}');

--Test Set5 : $min Operator 
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$min": { "a.$[]": 3 } } }', '{}');

--Test Set6 : $max Operator
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$max": { "a.$[]": 3 } } }', '{}');

--Test Set7 : $mul Operator
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$mul": { "a.$[]": 3 } } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1.0,2.2,3.2,4.3] }', '{ "": { "$mul": { "a.$[]": 3 } } }', '{}');

--Test Set8 : $bit Operator
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [14,6] }', '{ "": { "$bit":  { "a.$[]": {"or" : 1 } } } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [14,6] }', '{ "": { "$bit":  { "a.$[]": {"and" : 1 } } } }', '{}');

--Test Set9 : $unset Operator
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$unset": { "a.$[]": 1 } } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [[1,2],[3,4],[5,6]] }', '{ "": { "$unset": { "a.$[].$[]": 10} } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [{"c":5}] }', '{ "": { "$unset": { "a.$[].c": 1 } } }', '{}');

--Test Set10 $addToSet
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [[14],[6]] }', '{ "": { "$addToSet":  { "a.$[]": "new" } } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "x": {"myArray": [{"a": [[1],[1]],"b": [[1],[2]]},{"a": [[1],[2]],"b": [[1],[2]]}]} }', '{ "": { "$addToSet": {"x.myArray.$[].a.$[]": "new","x.myArray.$[].b.$[]": "new2" } } }', '{}');

--Test Set11 $push
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [[1],[2],[3],[4]] }', '{ "": { "$push": { "a.$[]": 30 } } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [[40,8,5],[42,7,15],[55,42,0],[41,0]] }', '{ "": { "$push": { "a.$[]": { "$sort":-1, "$each" : [10,20,30], "$position":2, "$slice":2} } } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1,"a" : { "b" : [ { "c" : 1, "d" : 2 } ] }},','{ "": { "$set": { "a.$[]abc.c": 11 } } }', '{}');

--Test Set12 : Update path not exist or cannot be created
SELECT documentdb_api_internal.bson_update_document('{"_id": 1,"a" : { "b" : [ 1 , 2 ], "H" : { "c" : 11 } }},','{ "": { "$set": { "a.b.$[].c": 11 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1,"a" : { "b" : [ { "c" : 1, "d" : 2 }, 2 ], "H" : { "c" : 11 } }},','{ "": { "$set": { "a.b.$[].c": 11 } } }', '{}');

--Test Set 13 : Positional all operator on non array element
SELECT documentdb_api_internal.bson_update_document('{"_id": 1,"a" : { "b" : [ { "c" : 1, "d" : 2 } ] }},','{ "": { "$set": { "a.$[].c": 11 } } }', '{}');

--Test Set 14 : Try with $ and $[<identifier>] and Typo with $[] 
SELECT documentdb_api_internal.bson_update_document('{"_id": 1,"a" : { "b" : [ { "c" : 1, "d" : 2 } ] }},','{ "": { "$set": { "a.$.c": 11 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1,"a" : { "b" : [ { "c" : 1, "d" : 2 } ] }},','{ "": { "$set": { "a.$[element].c": 11 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1.0,2.2,3.2,4.3] }', '{ "": { "$mul": { "a.$[x]": 3 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$mul": { "a.$[]abc": 3 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$set": {"$[]":10} } }', '{}');

--Test Set 15 : Complex One
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [[1,2],[3,4],[5,6]] }', '{ "": { "$inc": { "a.$[].$[]": 10 } } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1,"a" : { "b" : [ { "x":[1,2,3,4] }, { "x": [100,2,3,4] } ]  }}','{ "": { "$inc": { "a.b.$[].x.0": 100 } } }', '{}');

--Test set 16: Large field
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa":5},{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa":6}] }', '{ "": { "$set": { "a.$[].aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa": 10 } } }', '{}');

--Test set 17: Multiple Operator
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4], "b":10 }', '{ "": { "$set": { "a.$[]": 30 }, "$inc": {"b" : 1} } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4], "b":{"x":10} }', '{ "": { "$set": { "b.x": 30 }, "$inc": {"a.$[]" : 1} } }', '{}');

--Test Set 18 : Path Conflicts 
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [[1,2],[3,4]] }', '{ "": { "$inc": { "a.$[].$[]": 10, "a.0": 11 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [[1,2],[3,4]] }', '{ "": { "$inc": { "a.0": 10, "a.$[].$[]": 11 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [[1,2],[3,4]] }', '{ "": { "$inc": { "a.0.1": 10, "a.$[].$[]": 11 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [[1,2],[3,4]] }', '{ "": { "$inc": { "a.$[].$[]": 11, "a.1.0": 10 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [[1,2],[3,4]] }', '{ "": { "$inc": { "a.b.$[]": 10, "a.b.0": 11 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$inc": { "a.$[]": 11, "a.0": 10 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$inc": { "a.0": 11, "a.$[]": 10 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$inc": { "a.0": 11 }, "$set":{"a.$[]": 11} } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$inc": { "a.$[]": 11 }, "$set":{"a.0": 11} } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [{ "x": 10 }, {"x": 11}] }', '{ "": { "$inc": { "a.$[].x": 11 }, "$set":{"a.0": 11} } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [{"x":10},{"y":11}] }', '{ "": { "$set": { "a.$[]": 30 }, "$inc": {"a.1.y" : 1} } }', '{}');

--Test Set 19 : Multiple Spec One Operator
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [[1,2],[3,4]] }', '{ "": { "$inc": { "a.$[].0": 30 , "a.$[].1" : 10} } }', '{}');
SELECT newDocument as bson_update_document FROM documentdb_api_internal.bson_update_document('{"_id": 1, "a": [{"b":1,"c":2,"x":{"y":3}}] }', '{ "": { "$inc": { "a.$[].b": 10 , "a.$[].c" : 20, "a.$[].x.y":30} } }', '{}');



---Test Set 20 : $ Operator---
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [[1,2],[3,4]] }', '{ "": { "$inc": { "a.0.$": 30 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$set": { "a.$": 30 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$mul": { "a.$": 30 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$inc": { "a.$": 30 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$min": { "a.$": 30 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [{"x": 5},{"y":10}] }', '{ "": { "$inc": { "a.$.c": 30 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$set": {"$":10} } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a":[{"x":5},{"y":10}] }', '{ "": { "$inc": {"a.$[].x": 10,"a.$.y": 12 } } }', '{}');

---Test Set 21 : $[<identifier>] Operator---
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$set": { "a.$[ele]": 30 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$mul": { "a.$[ele]": 30 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$inc": { "a.$[ele]": 30 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [1,2,3,4] }', '{ "": { "$min": { "a.$[ele]": 30 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [{"x": 5,"c" :11},{"y":10},{"x":10}] }', '{ "": { "$inc": { "a.$[ele].c": 30 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$set": {"$[]":10} } }', '{}');


---Test Set 22 : Multiple positional operator on same level---
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a":[{"a":[1,2,3],"b":[4,5,6]}] }', '{ "": { "$inc": {"a.$[].a.$[]": 10,"a.$[].b.$[x]": 11 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a":[{"x":5},{"y":10}] }', '{ "": { "$inc": {"a.$[].x": 10,"a.$[x].y": 12 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a":[{"y":5},{"x":10}] }', '{ "": { "$inc": {"a.$[].x": 10,"a.$[x].y": 12 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [[1,2,3],[4,5,6]] }', '{ "": { "$inc": { "a.$.$[]": 10} } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": [ {"a":[ {"x":1},{"y":2} ] ,"b": [ {"x":5},{"y":10} ] } ] }', '{ "": { "$inc": { "a.$[].b.$[].x": 10, "a.$[].b.$[x].y":12} } }', '{}');


-- Test Set 23: Rename with positional should not work.
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$rename": {"a.$[]": "b.$[]" } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$rename": {"a.0": "b.$[]" } } }', '{}');

-- Test Set 24: $positional with adding new fields
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$set": {"a.c.$[]": 1 } } }', '{}');
SELECT documentdb_api_internal.bson_update_document('{"_id": 1, "a": { "b": 2 } }', '{ "": { "$set": {"c.$[]": 1 } } }', '{}');
