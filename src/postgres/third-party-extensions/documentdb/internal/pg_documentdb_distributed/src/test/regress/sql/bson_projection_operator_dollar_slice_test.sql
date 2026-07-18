SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 290000;
SET documentdb.next_collection_id TO 2900;
SET documentdb.next_collection_index_id TO 2900;

--Input type positive number
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : 0} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : 1} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : 2} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : 8} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : { "$numberLong" : "2"}} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : { "$numberDouble" : "2.1234"}} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : { "$numberDecimal" : "2.1234"}} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : []}', '{"a" : { "$slice" : 1} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1]}', '{"a" : { "$slice" : 1} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [{"x":[1,2,3]}, {"x":[4,5,6]}]}', '{"a.x" : { "$slice" : 2} }',NULL);

--Input type is Negative number
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : -0} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : -1} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : -2} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : -4} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5,6,7,8,9,10,11] }', '{"a" : { "$slice" : -13} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : -8} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : { "$numberLong" : "-2"}} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : { "$numberDouble" : "-2.1234"}} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : { "$numberDecimal" : "-2.1234"}} }',NULL);

--Input type is array with postive number fields
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [0,0]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [0,2]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [2,3]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [3,1]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [3,10]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : [4,10]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : [5,10]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [{ "$numberLong" : "1"}, { "$numberLong" : "3"} ] }}',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [{ "$numberDouble" : "2.1234"}, { "$numberDouble" : "3.1234"} ] }}',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [{ "$numberDecimal" : "1.1234"}, { "$numberDecimal" : "3.1234"}] }}',NULL);

--Input type is array with negative number fields
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [-0,0]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [-1,2]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [-2,3]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [-3,1]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [-3,10]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : [-4,10]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : [-5,10]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3] }', '{"a" : { "$slice" : [-10,10]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [{ "$numberLong" : "-1"}, { "$numberLong" : "3"} ] }}',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [{ "$numberDouble" : "-2.1234"}, { "$numberDouble" : "3.1234"} ] }}',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3,4,5], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [{ "$numberDecimal" : "-1.1234"}, { "$numberDecimal" : "3.1234"}] }}',NULL);

--Input is NaN or Infinity
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : { "$numberDecimal" : "NaN"}} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : { "$numberDecimal" : "Infinity"}} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : { "$numberDouble" : "NaN"}} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : { "$numberDouble" : "Infinity"}} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [{ "$numberDecimal" : "NaN"}, { "$numberDecimal" : "NaN"}]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [{ "$numberDecimal" : "NaN"}, { "$numberDecimal" : "Infinity"}]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [{ "$numberDecimal" : "Infinity"}, { "$numberDecimal" : "NaN"}]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [{ "$numberDecimal" : "Infinity"}, { "$numberDecimal" : "Infinity"}]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [0, { "$numberDecimal" : "Infinity"}]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [{ "$numberDecimal" : "Infinity"}, 1]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [0, { "$numberDecimal" : "NaN"}]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [{ "$numberDecimal" : "NaN"}, 1]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : { "$numberDecimal" : "-Infinity"}} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : [{ "$numberDecimal" : "-Infinity"}, 1]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : [1, { "$numberDecimal" : "-Infinity"}]} }',NULL);

--large values $slice
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : { "$numberDecimal" : "999999999999999999999999999999"}} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [{ "$numberDecimal" : "999999999999999999999999999999"}, { "$numberDecimal" : "999999999999999999999999999999"}] } }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : [{ "$numberDecimal" : "0"}, { "$numberDecimal" : "999999999999999999999999999999"}] } }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22}', '{"a" : { "$slice" : {"$numberDecimal" : "123123123123123121231"}} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22}', '{"a" : { "$slice" : [ {"$numberDecimal" : "123123123123123121231"}, {"$numberDecimal" : "123123123123123121231"} ]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22}', '{"a" : { "$slice" : [1,{"$numberDecimal" : "123123123123123121231"}]} }',NULL);

--inclusion Exclusion check
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : 1}, "b" :0 }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : 1}, "b" :1 }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : 1}, "b":0, "c":0 }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : 1}, "c": 1, "d" : 0 }',NULL);

--Nested array inside document & array
select bson_dollar_project_find('{"_id":"1", "a" : {"x": [1,2,3], "y":5 }, "b":10}', '{"a.x" : { "$slice" : 1}, "b" :0 }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : {"x": [1,2,3], "y":5 }, "b":10}', '{"a.x" : { "$slice" : 1}, "b" :1 }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [[1,2,3]], "b":10}', '{"a.0" : { "$slice" : 1}}',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [[1,2,3], {"y": 5}], "b":10}', '{"a.0" : { "$slice" : 1}, "b" :1 }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [[1,2,3],4,5]}', '{"a.0" : { "$slice" : 1}}',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [[1,2,3], {"y": 5}], "b":10}', '{"a.0" : { "$slice" : 1}, "b" :0 }',NULL);


--multiple Slice
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : 1}, "c" : { "$slice" : 1} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : 1}, "c" : { "$slice" : 1}, "b" :0 }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : 1}, "c" : { "$slice" : 1}, "b" :1 }',NULL);

--$slice with other operators
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : 1}, "c" : { "$elemMatch" : {"$gt":1}} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$elemMatch" : {"$gt":1}}, "c" : { "$slice" : 1}}',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : 1}, "c" : { "$elemMatch" : {"$gt":1}}, "b" : 1 }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$elemMatch" : {"$gt":1}}, "c" : { "$slice" : 1}, "b" :0}',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$slice" : 1}, "c" : { "$elemMatch" : {"$gt":1}}, "b" : 0 }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"a" : { "$elemMatch" : {"$gt":1}}, "c" : { "$slice" : 1}, "b" :1}',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"b" : 0 , "a" : { "$slice" : 1}, "c" : { "$elemMatch" : {"$gt":1}} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3], "b" : 22, "c" : [1,2,3], "d":4  }', '{"b" : 1 , "a" : { "$elemMatch" : {"$gt":1}}, "c" : { "$slice" : 1}}',NULL);
select bson_dollar_project_find('{"a": [ {"b" : [10]}, { "b" : [20]}]}', '{ "a.b": {"$slice":1}, "a.b": {"$add":[1,5]} }','{}');
select bson_dollar_project_find('{"a": [ {"b" : [10]}, { "b" : [20]}]}', '{ "a.b": {"$add":[1,10]}, "a.b": {"$slice":1} }','{}');
select bson_dollar_project_find('{"b": [[1,2,3,4]], "a": [10,20,30,40]}', '{ "a.$": 1, "b" : {"$slice" : 1} }','{"a" : {"$gt" :1}}');
select bson_dollar_project_find('{"_id":"1", "a" : {"b": {"c":[1,2,3], "d":10},"f":10 } }', '{"a.b.c" : {"$slice": 1}, "a.b.d": {"field":"$a.f"} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : {"b": {"c":[1,2,3], "d":10},"f":10 } }', '{"a.b.c" : {"$slice": 1}, "a.b.d": {"$isArray":"$a.c"} }',NULL);

--Path Collision 
select bson_dollar_project_find('{"_id":"1", "a" : [{"x":1, "y":2}]}', '{"a" : { "$slice" : 1}, "a.x":0 }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [{"x":1, "y":2}]}', '{"a" : { "$slice" : 1}, "a.x":1 }',NULL);

--array of array
select bson_dollar_project_find('{"_id":"1", "a" : [[{"b": [1,2,3]}]]}', '{"a.b" : { "$slice" : 2}}',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [[{"b": [1,2,3]}],  { "b": [1,2,3]}]}', '{"a.b" : { "$slice" : 2}}',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [[{"b": [1,2,3], "c":{ "d": [1,2,3] }  }],  { "e": [1,2,3] , "f": [[{"g": [1,2,3]}]] }]}', '{"a.b" : { "$slice" : 2}, "a.c.d" : { "$slice" : 2}, "a.e" : { "$slice" : 2},"a.f.g" : { "$slice" : 2}}',NULL);

--Null and undefined input
select bson_dollar_project_find('{"_id":"1", "a" : null}', '{"a" : { "$slice" : [1,2]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : [1,null]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : [null,1]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : [null,null]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : { "$undefined" : true }}', '{"a" : { "$slice" : [1,2]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : [1,{ "$undefined" : true }]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : [{ "$undefined" : true },1]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : [{ "$undefined" : true },{ "$undefined" : true }]} }',NULL);

--Invalid Input Negative cases find slice projection
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : "str"} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : ["str"]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : [1,2,3]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : [1,"str"]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : [1,-1]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : [1,0]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : [1]} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : null} }',NULL);
select bson_dollar_project_find('{"_id":"1", "a" : [1,2,3]}', '{"a" : { "$slice" : { "$undefined" : true }} }',NULL);

