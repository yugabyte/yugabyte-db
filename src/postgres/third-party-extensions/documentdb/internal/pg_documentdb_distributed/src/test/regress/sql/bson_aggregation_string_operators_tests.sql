SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal;
SET citus.next_shard_id TO 620000;
SET documentdb.next_collection_id TO 6200;
SET documentdb.next_collection_index_id TO 6200;

-- $concat operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : []} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : "apple"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : ["apple"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : "ℹ️ ❤️ documentdb"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : ["a","b"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : ["apple ","is ","a ","fruit", " ", "."]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : ["ℹ️","❤️","documentdb"]} }');

-- $concat operator: with literals and operators test:
select bson_dollar_project('{"_id":"1", "x": "1"}', '{"result" : { "$concat" : "$x"} }');
select bson_dollar_project('{"_id":"1", "x": "3"}', '{"result" : { "$concat" : ["1", {"$concat": ["foo","$x","bar"]}, "2"] } }');
select bson_dollar_project('{"_id":"1", "product": "mango", "qty": "10"}', '{"result" : { "$concat" : ["$product","-","$qty"]} }');

-- $concat operator: null undefined test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : null} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : [null]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : ["apple", "is", "a", null, "fruit"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : {"$undefined": true}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : [{"$undefined": true}]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : ["apple", "is", "a", {"$undefined": true} , "fruit"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : ["$a","$b"]} }');

--$concat with large input data
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : ["string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2","string1", "string2"]} }');

-- $concat operator: Unicode representation and escape sequences  test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : ["a","b\u0000"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : ["a","b\u000Ac"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : ["a","b\u0022"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : ["a","b\u000C"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : ["Copyright 2023 ","\u00A9 ","Concatstring ", "Inc.", " All rights reserved"]} }');

-- $concat operator: Negative test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : 1} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : true} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : 1.2} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : [1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : [{"$numberDecimal": "10"}]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$concat" : ["apple", "is", 1, "fruit"]} }');
select bson_dollar_project('{"_id":"1", "x": ["hello ","this ","is ","test"]}', '{"result" : { "$concat" : "$x"} }');

-- $split operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["hello, this is a test case"," "]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["mango,banana,lemon,grapes",","]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["I❤️documentdb","❤️"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["1abcd2abcd3abcd4abcd","abcd"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["app","apple"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["app","app"]} }');

-- $split operaator: delimeter in front and end
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["abcabcabc","abc"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["testcase    "," "]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["   testcase   "," "]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["testcase   "," "]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["test  case   "," "]} }');

-- $split operator: with literals and operators test:
select bson_dollar_project('{"_id":"1", "x": "apple is a fruit", "y" : " "}', '{"result" : { "$split" : ["$x","$y"]} }');
select bson_dollar_project('{"_id":"1", "x": "name,age,address,height,weight", "y" : ","}', '{"result" : { "$split" : ["$x","$y"]} }');
select bson_dollar_project('{"_id":"1","a":"ram"}', '{"result" : { "$split" : [{"$concat" : ["apple ","is ", "a " , "fruit "] }," "]} }');
select bson_dollar_project('{"_id":"1","a":"ram"}', '{"result" : { "$split" : [{"$concat" : ["apple is a fruit"] }, {"$concat" : [" "] }]} }');

-- $split operator: null undefined output test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : [null,null]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : [{"$undefined": true},{"$undefined": true}]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : [1,null]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : [{"$undefined": true},"app"]} }');
select bson_dollar_project('{"_id":"1", "a": "apple is a fruit"}', '{"result" : { "$split" : ["$a","$b"]} }');

-- $split operator: Unicode representation and escape sequences  test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["a\u0000b\u0000c\u0000d","\u0000"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["\u000A","\u000A"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["\u0022\u00A9\u0022\u00A9\u0022\u00A9\u0022","\u00A9"]} }');

-- $split operator: Negative test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : "apple"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["apple", ""]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : [1, "test"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["test", 1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : [{"$numberDecimal" : "1.231"}, 1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : []} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : null} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : [null]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["a"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : {"$undefined": true}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : [{"$undefined": true}]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : ["a","b","c"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$split" : [1]} }');

-- $strLenBytes operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : ""} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : "A"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : "Apple"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : "🙈"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : "I use to eat one apple 🍎 a day that became reason of my breakup 💔 as one apple a day keeps doctor away and she👧🏽 was a doctor 🧑🏽‍⚕️"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : ["Apple"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : "abcdefghijklmnopqrstuvwxyz"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : "1234567890"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : "!@#$%^&*(){}[];:<>?"} }');

-- $strLenBytes operator: Unicode representation and escape sequences  test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : ["I ❤️ documentdb"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : "I ❤️ documentdb"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : "A\u0000B"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : "\uA000BCD"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : "😀😁😂🤣😃😄😅😆😉😊😋😎🥲🙈🤔😪😟"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : "👩🏽👨🏽🧑🏽👧🏽👦🏽🧒🏽👨🏽‍🦰👩🏽‍🦰🧓🏽👴🏽👵🏽👶🏽🧑🏽‍🦰👩🏽‍🦱👨🏽‍🦱🧑🏽‍🦱👩🏽‍🦲👨🏽‍🦲🧑🏽‍🦲👩🏽‍🦳👨🏽‍🦳🧑🏽‍🦳👱🏽‍♀️👱🏽‍♂️👱🏽👸🏽🤴🏽🫅🏽👳🏽‍♀️👳🏽‍♂️👳🏽👲🏽🧔🏽🧔🏽‍♂️🧔🏽‍♀️👼🏽"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : ["\u0022\u00A9\u0022\u00A9\u0022\u00A9\u0022\u00A9"]} }');


-- $strLenBytes operator: literals and operators :
select bson_dollar_project('{"_id":"1", "a" : "One apple a day, keeps doctor away"}', '{"result" : { "$strLenBytes" : "$a"} }');
select bson_dollar_project('{"_id":"1", "a" : "One apple a day, keeps doctor away"}', '{"result" : { "$strLenBytes" :  {"$concat" : ["$a", "Apple is a fruit"]} } }');

-- $strLenBytes operator: Negative test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : 1} }');
select bson_dollar_project('{"_id":"1", "a" : 1}', '{"result" : { "$strLenBytes" : "$a"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : [1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : ["apple","cat"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : [1,"cat"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : ["cat",1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : null} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : [null]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : {"$undefined": true}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : [{"$undefined": true}]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenBytes" : ["$a"]} }');

-- $strLenCP operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : ""} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : "A"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : "Apple"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : "🙈"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : "I use to eat one apple 🍎 a day that became reason of my breakup 💔 as one apple a day keeps doctor away and she👧🏽 was a doctor 🧑🏽‍⚕️"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : ["Apple"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : "abcdefghijklmnopqrstuvwxyz"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : "1234567890"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : "!@#$%^&*(){}[];:<>?"} }');

-- $strLenCP operator: Unicode representation and escape sequences  test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : ["I ❤️ documentdb"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : "I ❤️ documentdb"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : "A\u0000B"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : "\uA000BCD"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : "😀😁😂🤣😃😄😅😆😉😊😋😎🥲🙈🤔😪😟"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : "👩🏽👨🏽🧑🏽👧🏽👦🏽🧒🏽👨🏽‍🦰👩🏽‍🦰🧓🏽👴🏽👵🏽👶🏽🧑🏽‍🦰👩🏽‍🦱👨🏽‍🦱🧑🏽‍🦱👩🏽‍🦲👨🏽‍🦲🧑🏽‍🦲👩🏽‍🦳👨🏽‍🦳🧑🏽‍🦳👱🏽‍♀️👱🏽‍♂️👱🏽👸🏽🤴🏽🫅🏽👳🏽‍♀️👳🏽‍♂️👳🏽👲🏽🧔🏽🧔🏽‍♂️🧔🏽‍♀️👼🏽"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : ["\u0022\u00A9\u0022\u00A9\u0022\u00A9\u0022\u00A9"]} }');


-- $strLenCP operator: literals and operators :
select bson_dollar_project('{"_id":"1", "a" : "One apple a day, keeps doctor away"}', '{"result" : { "$strLenCP" : "$a"} }');
select bson_dollar_project('{"_id":"1", "a" : "One apple a day, keeps doctor away"}', '{"result" : { "$strLenCP" :  {"$concat" : ["$a", "Apple is a fruit"]} } }');

-- $strLenCP operator: Negative test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : 1} }');
select bson_dollar_project('{"_id":"1", "a" : 1}', '{"result" : { "$strLenCP" : "$a"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : [1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : ["apple","cat"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : [1,"cat"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : ["cat",1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : null} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : [null]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : {"$undefined": true}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : [{"$undefined": true}]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strLenCP" : ["$a"]} }');

--$trim operator : basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "", "chars" :""}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "apple", "chars" :"apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "111apple111", "chars" :"1"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "123apple321", "chars" :"333333222222111111"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "apple ", "chars" :"pl"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": " apple ", "chars" :" ppl"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": " apple ", "chars" :" "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "apple ", "chars" :" "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": " apple", "chars" :" "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "ABCDOGABC", "chars" :"ABC"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "Remove this characters AAAAAAAAAAAAAAAAAA Remove this too", "chars" :"Removthiscarto "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "1234apple5678", "chars" :"12345678"}} }');

--$trim operator : without chars test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": ""}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": " apple "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "ABCDOGABC"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "apple "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": " apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "\napple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": " \u0000 \n apple \t \u200A "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": " \u00E2apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": " \u00E2apple\u00E2"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": " \u0080\u00E2apple\u0080"}} }');

--$trim operator : Unicode representation and escape sequences test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "\n I 💗 documentdb \n", "chars" :"\n "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "😀😀😎😎😋😎😎😁😁", "chars" :"😁😀"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "\napple", "chars" :"\n "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": " \u0000 \n apple \t \u200A ", "chars" :" \u0000 \n\t\u200A"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": " \u00E2apple", "chars" :"\u00E2 "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": " \u00E2apple\u00E2", "chars" :" \u00E2"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": " \u0080\u00E2apple\u0080", "chars" :"\u0080\u00E2"}} }');

-- $trim operator: literals and operators :
select bson_dollar_project('{"_id":"1", "a" : "0  this is test  0", "b": "0 " }', '{"result" : { "$trim" : {"input": "$a","chars": "$b"}} }');
select bson_dollar_project('{"_id":"1", "a" : "  0this is test0  ", "b": "0 " }', '{"result" : { "$trim" : {"input": "$a"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": {"$concat" : ["1","2","3","string","1","2","3"]}, "chars": {"$concat":["12","3"]} }} }');

-- $trim operator: null test:
select bson_dollar_project('{"_id":"1", "a" : "0  this is test  0", "b": "0 " }', '{"result" : { "$trim" : {"input": "$z","chars": "$b"}} }');
select bson_dollar_project('{"_id":"1", "a" : "0  this is test  0", "b": "0 " }', '{"result" : { "$trim" : {"input": "$a","chars": "$z"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": null, "chars" :"apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "apple", "chars" :null}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": {"$undefined" : true}, "chars" :"apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "apple", "chars" :{"$undefined" : true}}} }');

-- $trim operator: Negative test :
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : 1} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": 1}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": 1, "unknowarG": "1"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$trim" : {"input": "", "chars" : ["apple"]}} }');
select bson_dollar_project('{"_id":"1", "a" : 1}', '{"result" : { "$trim" : {"input": "$a"}} }');
select bson_dollar_project('{"_id":"1", "a" : 1}', '{"result" : { "$trim" : {"input": "apple", "chars" : "$a"}} }');


--$ltrim operator : basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "", "chars" :""}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "apple", "chars" :"apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "111apple", "chars" :"1"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "123apple", "chars" :"333333222222111111"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": " apple", "chars" :" "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "apple ", "chars" :" "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "ABCDOG", "chars" :"ABC"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "Remove this characters AAAAAAAAAAAAAAAAAA", "chars" :"Removthiscarto "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "1234apple", "chars" :"12345678"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "1234apple5678", "chars" :"12345678"}} }');

--$ltrim operator : without chars test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": ""}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": " apple "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "ABCDOGABC"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "apple "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": " apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "\napple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": " \u0000 \n apple \t \u200A "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": " \u00E2apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": " \u00E2apple\u00E2"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": " \u0080\u00E2apple\u0080"}} }');

--$ltrim operator : Unicode representation and escape sequences test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "\n I 💗 documentdb \n", "chars" :"\n "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "😀😀😎😎😋😎😎😁😁", "chars" :"😁😀"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "\napple", "chars" :"\n "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": " \u0000 \n apple \t \u200A ", "chars" :" \u0000 \n\t\u200A"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": " \u00E2apple", "chars" :"\u00E2 "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": " \u00E2apple\u00E2", "chars" :" \u00E2"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": " \u0080\u00E2apple\u0080", "chars" :"\u0080\u00E2"}} }');

-- $ltrim operator: literals and operators :
select bson_dollar_project('{"_id":"1", "a" : "0  this is test  0", "b": "0 " }', '{"result" : { "$ltrim" : {"input": "$a","chars": "$b"}} }');
select bson_dollar_project('{"_id":"1", "a" : "  0this is test0  ", "b": "0 " }', '{"result" : { "$ltrim" : {"input": "$a"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": {"$concat" : ["1","2","3","string","1","2","3"]}, "chars": {"$concat":["12","3"]} }} }');

-- $ltrim operator: null test:
select bson_dollar_project('{"_id":"1", "a" : "0  this is test  0", "b": "0 " }', '{"result" : { "$ltrim" : {"input": "$z","chars": "$b"}} }');
select bson_dollar_project('{"_id":"1", "a" : "0  this is test  0", "b": "0 " }', '{"result" : { "$ltrim" : {"input": "$a","chars": "$z"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": null, "chars" :"apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "apple", "chars" :null}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": {"$undefined" : true}, "chars" :"apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "apple", "chars" :{"$undefined" : true}}} }');

-- $ltrim operator: Negative test :
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : 1} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": 1}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": 1, "unknowarG": "1"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$ltrim" : {"input": "", "chars" : ["apple"]}} }');
select bson_dollar_project('{"_id":"1", "a" : 1}', '{"result" : { "$ltrim" : {"input": "$a"}} }');
select bson_dollar_project('{"_id":"1", "a" : 1}', '{"result" : { "$ltrim" : {"input": "apple", "chars" : "$a"}} }');

--$rtrim operator : basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "", "chars" :""}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "apple", "chars" :"apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "apple111", "chars" :"1"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "apple321", "chars" :"333333222222111111"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "apple ", "chars" :"pl"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "apple ", "chars" :" ppl"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "apple ", "chars" :" "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "DOGABC", "chars" :"ABC"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "Remove this characters AAAAAAAAAAAAAAAAAA Remove this too", "chars" :"Removthiscarto "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "1234apple5678", "chars" :"12345678"}} }');

--$rtrim operator : without chars test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": ""}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": " apple "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "DOGABC"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "apple "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": " apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "\napple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": " \u0000 \n apple \t \u200A "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": " \u00E2apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": " \u00E2apple\u00E2"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": " \u0080\u00E2apple\u0080"}} }');

--$rtrim operator : Unicode representation and escape sequences test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "\n I 💗 documentdb \n", "chars" :"\n "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "😀😀😎😎😋😎😎😁😁", "chars" :"😁😀"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "\napple", "chars" :"\n "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": " \u0000 \n apple \t \u200A ", "chars" :" \u0000 \n\t\u200A"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": " \u00E2apple", "chars" :"\u00E2 "}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": " \u00E2apple\u00E2", "chars" :" \u00E2"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": " \u0080\u00E2apple\u0080", "chars" :"\u0080\u00E2"}} }');

-- $rtrim operator: literals and operators :
select bson_dollar_project('{"_id":"1", "a" : "0  this is test  0", "b": "0 " }', '{"result" : { "$rtrim" : {"input": "$a","chars": "$b"}} }');
select bson_dollar_project('{"_id":"1", "a" : "  0this is test0  ", "b": "0 " }', '{"result" : { "$rtrim" : {"input": "$a"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": {"$concat" : ["1","2","3","string","1","2","3"]}, "chars": {"$concat":["12","3"]} }} }');

-- $rtrim operator: null test:
select bson_dollar_project('{"_id":"1", "a" : "0  this is test  0", "b": "0 " }', '{"result" : { "$rtrim" : {"input": "$z","chars": "$b"}} }');
select bson_dollar_project('{"_id":"1", "a" : "0  this is test  0", "b": "0 " }', '{"result" : { "$rtrim" : {"input": "$a","chars": "$z"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": null, "chars" :"apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "apple", "chars" :null}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": {"$undefined" : true}, "chars" :"apple"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "apple", "chars" :{"$undefined" : true}}} }');

-- $rtrim operator: Negative test :
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : 1} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": 1}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": 1, "unknowarG": "1"}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$rtrim" : {"input": "", "chars" : ["apple"]}} }');
select bson_dollar_project('{"_id":"1", "a" : 1}', '{"result" : { "$rtrim" : {"input": "$a"}} }');
select bson_dollar_project('{"_id":"1", "a" : 1}', '{"result" : { "$rtrim" : {"input": "apple", "chars" : "$a"}} }');

--$indexOfBytes operator : basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["",""]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["apple","ple"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["appleple","ple"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["appleple","ple",0,10]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["appleple","ple",10,0]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["appleple","xyz"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["","",1,3]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["appleple","ple",4]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["appleple","ple",4,5]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["appleple","ple",4,6]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["appleple","ple",2147483647,7]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["appleple","ple",1,2147483647]} }');

--$indexOfBytes operator : Unicode representation and escape sequences test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["\u0000\u00E2\t\n\u200A\u0080","\n\u200A"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["\u0000\u00E2\t\n\u200A\u0080","\u0080",4,7]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["\u0000\u00E2\t\n\u200A\u0080","\u0080",7]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["I ❤️ documentdb ","❤️"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["😁😲😎🧔🏽‍♂️🧔🏽‍♂️🧔🏽‍♂️😁😲😎","🧔🏽‍♂️🧔🏽‍♂️"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["😁😲😎🧔🏽‍♂️🧔🏽‍♂️🧔🏽‍♂️😁😲😎","🧔🏽‍♂️🧔🏽‍♂️",2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["😁😲😎🧔🏽‍♂️🧔🏽‍♂️🧔🏽‍♂️😁😲😎","🧔🏽‍♂️🧔🏽‍♂️",2,4]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["😁😲😎🧔🏽‍♂️🧔🏽‍♂️🧔🏽‍♂️😁😲😎","🧔🏽‍♂️🧔🏽‍♂️",3,4]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["😁😟😴🥲😀😋😎🙈🧔🏽‍♂️🧔🏽🧔🏽‍♂️🧔🏽‍♀️😲😁❤️🕷️🙈💔👳🏽‍♂️","😎🙈🧔🏽‍♂️🧔🏽🧔🏽‍♂️🧔🏽‍♀️"]} }');

-- $indexOfBytes operator: literals and operators :
select bson_dollar_project('{"_id":"1","a":"1234567", "b":"456","c":2, "d":9 }', '{"result" : { "$indexOfBytes" : ["$a","$b","$c","$d"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : [{"$concat" : ["a","p","p","l","e"]},{"$concat" : ["p","l"]},{"$add": [0,1]},{"$add" :[3,2]}]} }');

-- $indexOfBytes operator: null test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : [null,"a"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : [{"$undefined": true},"a"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["$x","y"]} }');

-- $indexOfBytes operator: Negative test :
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : 1} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : []} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["a"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : [1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["a",1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["a",null]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["a","$x"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["a","b",-1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["a","b","x"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["a","b",1,-3]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["a","b",1,"z"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["a","b",1,"$t"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["appleple","ple",2147483648,7]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["appleple","ple",1,2147483648]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["appleple","ple",1.2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfBytes" : ["appleple","ple",1,1.3]} }');

--$indexOfCP operator : basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["",""]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["apple","ple"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["appleple","ple"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["appleple","ple",0,10]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["appleple","ple",10,0]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["appleple","xyz"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["appleple","ple",4]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["","",2,3]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["appleple","ple",4,5]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["appleple","ple",4,6]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["appleple","ple",2147483647,7]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["appleple","ple",1,2147483647]} }');

--$indexOfCP operator : Unicode representation and escape sequences test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["\u0000\u00E2\t\n\u200A\u0080","\n\u200A"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["\u0000\u00E2\t\n\u200A\u0080","\u0080",7]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["I ❤️ documentdb ","❤️"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["😁😲😎🧔🏽‍♂️🧔🏽‍♂️🧔🏽‍♂️😁😲😎","🧔🏽‍♂️🧔🏽‍♂️"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["😁😲😎🧔🏽‍♂️🧔🏽‍♂️🧔🏽‍♂️😁😲😎","🧔🏽‍♂️🧔🏽‍♂️",2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["😁😲😎🧔🏽‍♂️🧔🏽‍♂️🧔🏽‍♂️😁😲😎","🧔🏽‍♂️🧔🏽‍♂️",2,4]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["😁😲😎🧔🏽‍♂️🧔🏽‍♂️🧔🏽‍♂️😁😲😎","🧔🏽‍♂️🧔🏽‍♂️",3,4]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["😁😟😴🥲😀😋😎🙈🧔🏽‍♂️🧔🏽🧔🏽‍♂️🧔🏽‍♀️😲😁❤️🕷️🙈💔👳🏽‍♂️","😎🙈🧔🏽‍♂️🧔🏽🧔🏽‍♂️🧔🏽‍♀️"]} }');

-- $indexOfCP operator: literals and operators :
select bson_dollar_project('{"_id":"1","a":"1234567", "b":"456","c":2, "d":9 }', '{"result" : { "$indexOfCP" : ["$a","$b","$c","$d"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : [{"$concat" : ["a","p","p","l","e"]},{"$concat" : ["p","l"]},{"$add": [0,1]},{"$add" :[3,2]}]} }');

-- $indexOfCP operator: null test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : [null,"a"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : [{"$undefined": true},"a"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["$x","y"]} }');

-- $indexOfCP operator: Negative test :
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : 1} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : []} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["a"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : [1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["a",1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["a",null]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["a","$x"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["a","b",-1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["a","b","x"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["a","b",1,-3]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["a","b",1,"z"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["a","b",1,"$t"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["appleple","ple",2147483648,7]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["appleple","ple",1,2147483648]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["appleple","ple",1.2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$indexOfCP" : ["appleple","ple",1,1.3]} }');

-- $toUpper operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : ""} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : null} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : "A"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : "Apple"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : "🙈"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : "I use to eat one apple 🍎 a day that became reason of my breakup 💔 as one apple a day keeps doctor away and she👧🏽 was a doctor 🧑🏽‍⚕️"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : ["Apple"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : "abcdefghijklmnopqrstuvwxyz"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : "1234567890"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : "!@#$%^&*(){}[];:<>?"} }');

-- $toUpper operator: Unicode representation and escape sequences  test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : ["I ❤️ documentdb"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : "I ❤️ documentdb"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : "A\u0000B"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : "a\u0000B\u000d"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : "\uA000Bcd"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : "a\u0000B\u0000\u0000z"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : "😀😁😂🤣😃😄😅😆😉😊😋😎🥲🙈🤔😪😟"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : ["\u0022\u00A9\u0022\u00A9\u0022\u00A9\u0022\u00A9"]} }');


-- $toUpper operator: numbers, integers decimal:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : 55.5} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : 1234567899877345} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : {"$numberInt": "3456"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : {"$numberLong": "3456213324342"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : {"$numberDecimal": "3456213324342.324789234567934"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : {"$numberDecimal": "1e10"} } }');


-- $toUpper operator: date and timestamps:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : {"$date": { "$numberLong" : "0" }} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : {"$date": { "$numberLong" : "86401" }} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : {"$date": { "$numberLong" : "-33563519937977" }} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : {"$timestamp": { "t" : 1671991326 , "i": 0}} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : {"$timestamp": { "t" : 0 , "i": 0}} } }');

-- $toUpper operator: expressions:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : {"$toUpper": "abcde" } } }');
select bson_dollar_project('{"_id":"2", "test": "this is a test"}', '{"result" : { "$toUpper" : "$test" } }');
select bson_dollar_project('{"_id":"2"}', '{"result" : { "$toUpper" : {"$concat":["a","b","c","1"]} } }');
select bson_dollar_project('{"_id":"2"}', '{"result" : { "$toUpper" : "$test" } }');

-- $toUpper operator: negative test cases :
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : ["a","b"] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : [1,"b"] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toUpper" : true } }');

-- $toLower operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : ""} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : null} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : "A"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : "Apple"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : "🙈"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : "I USE To Eat One aPple 🍎 a dAy That Became rEasOn of my breakup 💔 as one apple a day keeps doctor away and she👧🏽 was a doctor 🧑🏽‍⚕️"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : ["aPPLE"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : "abcLHKSFHdefghijklmnopqrstuvwxyz"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : "1234567890"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : "!@#$%^&*(){}[];:<>?"} }');

-- $toLower operator: Unicode representation and escape sequences  test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : ["I ❤️ documentdb"]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : "I ❤️ documentdb"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : "A\u0000B"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : "a\u0000B\u000d"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : "\uA000Bcd"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : "a\u0000B\u0000\u0000z"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : "😀😁😂🤣😃😄😅😆😉😊😋😎🥲🙈🤔😪😟"} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : ["\u0022\u00A9\u0022\u00A9\u0022\u00A9\u0022\u00A9"]} }');


-- $toLower operator: numbers, integers decimal:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : 55.5} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : 1234567899877345} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : {"$numberInt": "3456"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : {"$numberLong": "3456213324342"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : {"$numberDecimal": "3456213324342.324789234567934"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : {"$numberDecimal": "1e10"} } }');

-- $toLower operator: date and timestamps:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : {"$date": { "$numberLong" : "0" }} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : {"$date": { "$numberLong" : "86400" }} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : {"$date": { "$numberLong" : "-33563519937977" }} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : {"$timestamp": { "t" : 1671991326 , "i": 0}} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : {"$timestamp": { "t" : 0 , "i": 0}} } }');

-- $toLower operator: expressions:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : {"$toUpper": "abcde" } } }');
select bson_dollar_project('{"_id":"2", "test": "THIS IS a test"}', '{"result" : { "$toLower" : "$test" } }');
select bson_dollar_project('{"_id":"2"}', '{"result" : { "$toLower" : {"$concat":["A","B","C","1"]} } }');
select bson_dollar_project('{"_id":"2"}', '{"result" : { "$toLower" : "$test" } }');

-- $toLower operator: negative test cases :
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : ["a","b"] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : [1,"b"] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$toLower" : true } }');

-- $strcasecmp operator: Unicode representation and escape sequences  test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [ "\u0080D\u20ac", "\u0080d\u20ac"] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [ "\u0080D\u20ac\u000D", "\u0080d\u20ac\u000d"] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [ "I ❤️ documentdb", "I ❤️ documentdb" ] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [ "😀😁😂🤣😃😄😅😆😉😊😋😎🥲🙈🤔😪😟", "😀😁😂🤣😃😄😅😆😉😊😋😎🥲🙈🤔😪😟" ] } }');


-- $strcasecmp operator: numbers, integers decimal:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : ["1.2345",1.2345] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : ["1.234512345566666234324",1.234512345566666234324] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [1.234512345566666234324, "1.234512345566666234324"] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [{"$numberDecimal": "1.234512345566666234324"}, "1.234512345566666234324"] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [{"$numberDecimal": "1.2e10"}, "1e10"] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [{"$numberDecimal": "1.2e10"}, "1e10"] } }');

-- $strcasecmp operator: date and timestamps:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [ "1970-01-01T00:00:00.000Z", {"$date": { "$numberLong" : "0" }} ] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [ "1970-01-01T00:00:00.000Z", {"$date": { "$numberLong" : "0" }} ] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [ "0906-06-01T08:01:02.023Z", {"$date": { "$numberLong" : "-33563519937977" }} ] } }');
-- select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [ "DEC 25 18:02:06:000", {"$timestamp": { "t" : 1671991326 , "i": 0}} ] } }');

-- $strcasecmp operator: expressions:
select bson_dollar_project('{"_id":"2", "test": "THIS IS a test"}', '{"result" : { "$strcasecmp" : ["$test", "this is a test"] } }');
select bson_dollar_project('{"_id":"2"}', '{"result" : { "$strcasecmp" : ["$test", ""] } }');


-- $strcasecmp operator: negative test cases :
 select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [ "0906-06-01T08:01:02.023Z", true ] } }');
 select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [ ""] } }');
 select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : ["", ["a"]] } }');
 select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [{"$binary": {"base64": "ww==", "subType": "01"}},"a"] } }');
 select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : ["", {"$regex": "/ab/cdsd/abc", "$options" : ""}] } }');
 select bson_dollar_project('{"_id":"1"}', '{"result" : { "$strcasecmp" : [ "" , "a2", 123] } }');


-- $substr operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["hello", 0 , 0]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["hello", 0 , 5]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["hello", 2 , 2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["hello", 2 , -1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["hello", 2, 3]} }');

-- $substr operator: numbers, integers decimal:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [55.5, 0, -1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [1234567899877345, 0, -1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [{"$numberInt": "3456"}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [{"$numberLong": "3456213324342"}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [{"$numberDecimal": "3456213324342.324789234567934"}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [{"$numberDecimal": "1e10"}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["This isa test", {"$numberLong": "2342342"}, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["This isa test", 0, {"$numberLong": "23423423"}] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["This isa test", {"$numberLong": "2"}, 1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["This isa test", {"$numberDecimal": "2342342.908123098234"}, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["This isa test", 3, {"$numberDecimal": "2.9934234"}] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["This isa test", 2.9, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["This isa test", 1, 4.2] } }');


-- $substr operator: date and time types
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [{"$date": { "$numberLong" : "0" }}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [{"$date": { "$numberLong" : "86400" }}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [{"$date": { "$numberLong" : "-33563519937977" }}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [{"$timestamp": { "t" : 1671991326 , "i": 0}}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [{"$timestamp": { "t" : 0 , "i": 0}}, 0, -1] } }');

-- $substr operator: Unicode representation and escape sequences  test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["I ❤️ documentdb", 1, -1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["A\u0000B", 1, 2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["a\u0000B\u000d", 0, -1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["a\u0000B\u0000\u0000z", 2, 2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["😀😁😂🤣😃😄😅😆😉😊😋😎🥲🙈🤔😪😟", 40, 16]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["\u0022\u00A9\u0022\u00A9\u0022\u00A9\u0022\u00A9", 0, -1]} }');

-- $substr operator: expressions:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [{"$toUpper": "abcde" }, 0, -1] } }');
select bson_dollar_project('{"_id":"2", "test": "THIS IS a test"}', '{"result" : { "$substr" : ["$test", 0, -1] } }');
select bson_dollar_project('{"_id":"2"}', '{"result" : { "$substr" : [{"$concat":["A","B","C","1"]}, 0, -1] } }');
select bson_dollar_project('{"_id":"2"}', '{"result" : { "$substr" : ["$test", 0, -1] } }');

-- $substr negative test cases:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["hello", -1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [[1, 2], 1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [{"a" : "b"}, -1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [{"$oid" : "639926cee6bda3127f153bf1"}, -1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [NaN, -1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["I ❤️ documentdb", 2, 4]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["\uA000Bcd", 1, 2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [{"$binary": {"base64": "ww==", "subType": "01"}}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : [{"$regex": "/ab/cdsd/abc", "$options" : ""}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["hello", null, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : ["hello", 1, null] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : {"a" : "b"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substr" : null } }');
select bson_dollar_project('{"_id":"1", "a" : {"b" : ["hello", 1, 2]}}', '{"result" : { "$substr" : "$a.b" } }');

-- $substrBytes operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["hello", 0 , 0]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["hello", 0 , 5]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["hello", 2 , 2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["hello", 2 , -1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["hello", 2, 3]} }');

-- $substrBytes operator: numbers, integers decimal:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [55.5, 0, -1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [1234567899877345, 3, 6]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [{"$numberInt": "3456"}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [{"$numberLong": "3456213324342"}, 6, 15] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [{"$numberDecimal": "3456213324342.324789234567934"}, 13, 5] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [{"$numberDecimal": "1e10"}, 2, 2] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["This isa test", {"$numberLong": "2342342"}, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["This isa test", 0, {"$numberLong": "23423423"}] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["This isa test", {"$numberLong": "2"}, 1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["This isa test", {"$numberDecimal": "2342342.908123098234"}, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["This isa test", 3, {"$numberDecimal": "2.9934234"}] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["This isa test", 2.9, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["This isa test", 1, 4.2] } }');

-- $substrBytes operator: date and time types
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [{"$date": { "$numberLong" : "0" }}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [{"$date": { "$numberLong" : "86400" }}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [{"$date": { "$numberLong" : "-33563519937977" }}, 5, 8] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [{"$timestamp": { "t" : 1671991326 , "i": 0}}, 4, 12] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [{"$timestamp": { "t" : 0 , "i": 0}}, 0, -1] } }');

-- $substrBytes operator: Unicode representation and escape sequences  test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["I ❤️ documentdb", 1, -1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["A\u0000B", 1, 2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["a\u0000B\u000d", 0, -1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["a\u0000B\u0000\u0000z", 2, 2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["😀😁😂🤣😃😄😅😆😉😊😋😎🥲🙈🤔😪😟", 40, 16]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["\u0022\u00A9\u0022\u00A9\u0022\u00A9\u0022\u00A9", 0, -1]} }');

-- $substrBytes operator: expressions:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [{"$toUpper": "abcde" }, 0, -1] } }');
select bson_dollar_project('{"_id":"2", "test": "THIS IS a test"}', '{"result" : { "$substrBytes" : ["$test", 0, -1] } }');
select bson_dollar_project('{"_id":"2"}', '{"result" : { "$substrBytes" : [{"$concat":["A","B","C","1"]}, 0, -1] } }');
select bson_dollar_project('{"_id":"2"}', '{"result" : { "$substrBytes" : ["$test", 0, -1] } }');

-- $substrBytes negative test cases:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["hello", -1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [[1, 2], 1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [{"a" : "b"}, -1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [{"$oid" : "639926cee6bda3127f153bf1"}, -1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [NaN, -1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["I ❤️ documentdb", 2, 4]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["\uA000Bcd", 1, 2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [{"$binary": {"base64": "ww==", "subType": "01"}}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [{"$regex": "/ab/cdsd/abc", "$options" : ""}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["hello", null, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : ["hello", 1, null] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : {"a" : "b"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : null } }');
select bson_dollar_project('{"_id":"1", "a" : {"b" : ["hello", 1, 2]}}', '{"result" : { "$substrBytes" : "$a.b" } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrBytes" : [null, 1, null] } }');

-- $substrCP operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["hello", 0 , 0]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["hello", 0 , 5]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["hello", 2 , 2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["hello", 2 , 51]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["hello", 2, 3]} }');

-- $substrCP operator: numbers, integers decimal:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [55.5, 0, 3]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [1234567899877345, 0, 9]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [{"$numberInt": "3456"}, 0, 4] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [{"$numberLong": "3456213324342"}, 0, 10] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [{"$numberDecimal": "3456213324342.324789234567934"}, 0, 25] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [{"$numberDecimal": "1e10"}, 0, 5] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["This isa test", {"$numberLong": "2342342"}, 5] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["This isa test", 0, {"$numberLong": "23423423"}] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["This isa test", {"$numberLong": "2"}, 1] } }');

-- $substrCP operator: date and time types
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [{"$date": { "$numberLong" : "0" }}, 0, 20] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [{"$date": { "$numberLong" : "86400" }}, 0, 20] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [{"$date": { "$numberLong" : "-33563519937977" }}, 0, 20] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [{"$timestamp": { "t" : 1671991326 , "i": 0}}, 0, 20] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [{"$timestamp": { "t" : 0 , "i": 0}}, 0, 20] } }');

-- $substrCP operator: special characters and escape sequences  test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["jalapeño", 4, 3]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["jalapeño", 6, 2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [ "cafétéria", 5, 4]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [ "cafétéria", 7, 3]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [ "寿司sushi", 0, 3]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["I ❤️ documentdb", 2, 1]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["I ❤️ documentdb", 1, 5]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["A\u0000B", 1, 2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["a\u0000B\u000d", 0, 2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["\uA000Bcd", 1, 2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$susbtrCP" : ["a\u0000B\u0000\u0000z", 2, 2]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["😀😁😂🤣😃😄😅😆😉😊😋😎🥲🙈🤔😪😟", 40, 13]} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["\u0022\u00A9\u0022\u00A9\u0022\u00A9\u0022\u00A9", 1, 2]} }');

-- $substrCP operator: expressions:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [{"$toUpper": "abcde" }, 0, 5] } }');
select bson_dollar_project('{"_id":"2", "test": "THIS IS a test"}', '{"result" : { "$substrCP" : ["$test", 0, 5] } }');
select bson_dollar_project('{"_id":"2"}', '{"result" : { "$substrCP" : [{"$concat":["A","B","C","1"]}, 0, 5] } }');
select bson_dollar_project('{"_id":"2"}', '{"result" : { "$substrCP" : ["$test", 0, 5] } }');
select bson_dollar_project('{"_id":"2"}', '{"output":{"$substrCP" : ["∫aƒ",0,{"$subtract" : [{"$strLenCP" : "∫aƒ"},0]}]}}');

-- $substrCP negative test cases:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["hello", -1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [[1, 2], 1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [{"a" : "b"}, -1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [{"$oid" : "639926cee6bda3127f153bf1"}, -1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [NaN, -1, 1]}}');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [{"$binary": {"base64": "ww==", "subType": "01"}}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [{"$regex": "/ab/cdsd/abc", "$options" : ""}, 0, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["hello", null, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["hello", 1, null] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : {"a" : "b"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : null } }');
select bson_dollar_project('{"_id":"1", "a" : {"b" : ["hello", 1, 2]}}', '{"result" : { "$substrCP" : "$a.b" } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : [null, 1, null] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["hello", 1.1, 1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["hello", 1, 1.1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["hello", -1, 1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["hello", 1, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["hello", 1, NaN] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["hello", NaN, 1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["This isa test", {"$numberDecimal": "2342342.908123098234"}, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["This isa test", 3, {"$numberDecimal": "2.9934234"}] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["This isa test", 2.9, -1] } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$substrCP" : ["This isa test", 1, 4.2] } }');

-- $regexMatch operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input":"", "regex" : "" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input":"a", "regex" : "a" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input":"a", "regex" : "b" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input":"v_core", "regex" : "Ru-Based" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input":"apple", "regex" : "apple" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input":"apple is a fruit", "regex" : "is" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input":"apple is a fruit", "regex" : "fruit" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input":"APPLE", "regex" : "apple" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input":"APPLE", "regex" : "apple", "options": "i" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input":"APPLE","regex" : {"$regex" : "apple", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"2"}', '{"result" : { "$regexMatch" : {"input":"Banana","regex" : {"$regex" : "banana", "$options" : "i"} }} }');

-- $regexMatch operator: complex regex pattern test:
select bson_dollar_project('{"_id":"4"}', '{"result" : { "$regexMatch" : {"input":"1234567890","regex" : "\\d{3}", "options" : "" }} }');
select bson_dollar_project('{"_id":"3"}', '{"result" : { "$regexMatch" : {"input":"Hello World","regex" : "\\bworld\\b", "options" : "i" }} }');
select bson_dollar_project('{"_id":"5"}', '{"result" : { "$regexMatch" : {"input":"Microsoft IDC","regex" : "id", "options" : "i" }} }');
select bson_dollar_project('{"_id":"6"}', '{"result" : { "$regexMatch" : {"input":"Hello, World!","regex" : "[,!]", "options" : "i" }} }');
select bson_dollar_project('{"_id":"7"}', '{"result" : { "$regexMatch" : {"input":"Lorem ipsum dolor sit amet, consectetur adipiscing elit.","regex" : "\\b\\w{5}\\b", "options" : "i" }} }');
select bson_dollar_project('{"_id":"8"}', '{"result" : { "$regexMatch" : {"input":"Email me at john@example.com or call 555-123-4567 for more information.","regex" : "\\b[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}\\b|\\b\\d{3}-\\d{3}-\\d{4}\\b", "options" : "i" }} }');
select bson_dollar_project('{"_id":"9"}', '{"result" : { "$regexMatch" : {"input":"<h1>Hello</h1><p>Paragraph 1</p><p>Paragraph 2</p>","regex" : "<p>(.*?)</p>", "options" : "i" }} }');

-- $regexMatch operator: complex regex pattern with BSON_TYPE_REGEX:
select bson_dollar_project('{"_id":"4"}', '{"result" : { "$regexMatch" : {"input":"1234567890","regex" : {"$regex" : "\\d{3}", "$options" : ""} }} }');
select bson_dollar_project('{"_id":"3"}', '{"result" : { "$regexMatch" : {"input":"Hello World","regex" : {"$regex" : "\\bworld\\b", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"5"}', '{"result" : { "$regexMatch" : {"input":"Microsoft IDC","regex" : {"$regex" : "id", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"6"}', '{"result" : { "$regexMatch" : {"input":"Hello, World!","regex" : {"$regex" : "[,!]", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"7"}', '{"result" : { "$regexMatch" : {"input":"Lorem ipsum dolor sit amet, consectetur adipiscing elit.","regex" : {"$regex" : "\\b\\w{5}\\b", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"8"}', '{"result" : { "$regexMatch" : {"input":"Email me at john@example.com or call 555-123-4567 for more information.","regex" : {"$regex" : "\\b[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}\\b|\\b\\d{3}-\\d{3}-\\d{4}\\b", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"9"}', '{"result" : { "$regexMatch" : {"input":"<h1>Hello</h1><p>Paragraph 1</p><p>Paragraph 2</p>","regex" : {"$regex" : "<p>(.*?)</p>", "$options" : "i"} }} }');

-- $regexMatch operator: captures in output:
select bson_dollar_project('{"_id":"10"}', '{"result" : { "$regexMatch" : {"input":"John Doe, 25 years old","regex" : {"$regex" : "(\\w+) (\\w+), (\\d+) years old", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"11"}', '{"result" : { "$regexMatch" : {"input":"Date: 2023-07-14","regex" : {"$regex" : "Date: (\\d{4}-\\d{2}-\\d{2})", "$options" : ""} }} }');
select bson_dollar_project('{"_id":"12"}', '{"result" : { "$regexMatch" : {"input":"Product: Apple iPhone 12","regex" : {"$regex" : "Product: (\\w+) (\\w+) (\\d+)", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"13"}', '{"result" : { "$regexMatch" : {"input":"Email: john@example.com","regex" : {"$regex" : "Email: ([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,})", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"14"}', '{"result" : { "$regexMatch" : {"input":"Lorem ipsum dolor sit amet, consectetur adipiscing elit.","regex" : {"$regex" : "(\\b\\w{5}\\b)", "$options" : ""} }} }');

-- $regexMatch operator: different options:
select bson_dollar_project('{"_id":"15"}', '{"result" : { "$regexMatch" : {"input":"Hello\nworld","regex" : {"$regex" : "^world", "$options" : "m"} }} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexMatch" : {"input":"The quick brown FOX","regex" : {"$regex" : "\\bfox\\b", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"17"}', '{"result" : { "$regexMatch" : {"input":"123\n456\n789","regex" : {"$regex" : "\\d+", "$options" : "g"}}}}');
select bson_dollar_project('{"_id":"18"}', '{"result" : { "$regexMatch" : {"input":"This is a long text","regex" : {"$regex" : "long text", "$options" : "is"} }} }');
select bson_dollar_project('{"_id":"20"}', '{"result" : { "$regexMatch" : {"input":"Hello there","regex" : {"$regex" : "Hello\\s there", "$options" : "x"} }} }');

-- $regexMatch operator: false cases:
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexMatch" : {"input":null,"regex" : {"$regex" : "", "$options" : ""} }
} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexMatch" : {"input":"a","regex" : null }} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexMatch" : {"input":"$z","regex" : {"$regex" : "", "$options" : ""} }
} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexMatch" : {"input":"a","regex" : "$z" }} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexMatch" : {"input":"apple","regex" : "boy" }} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexMatch" : {"input":"The quick brown FOX","regex" : {"$regex" : "\\bfox\\b", "$options" : ""} }} }');
select bson_dollar_project('{"_id":"19"}', '{"result" : { "$regexMatch" : {"input":"This is a\nmultiline\nstring","regex" : {"$regex" : "multiline string", "$options" : "ix"} }} }');

-- $regexMatch operator: expression cases:
select bson_dollar_project('{"_id":"1","a":"apple", "b":"app"}', '{"result" : { "$regexMatch" : {"input":"$a", "regex" : "$b" }} }');
select bson_dollar_project('{"_id":"1","a":"APPLE", "b":"app", "c" : "i"}', '{"result" : { "$regexMatch" : {"input":"$a", "regex" : "$b", "options" :"$c" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input": {"$concat" : ["APPLE", "IS", "FRUIT"]}, "regex" : {"$toLower" : "APPLE"}, "options": {"$concat" : "i"} }} }');

-- $regexMatch operator: negative test cases :
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input":"", "regex" : "", "extra" : "" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : { "regex" : ""}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : { "input" : ""}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input": 1, "regex" : "", "options" : "" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input": "a", "regex" : 1, "options" : "" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input": "a", "regex" : "a", "options" : 1 }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input": "a", "regex" : {"$regex" : "", "$options": "i"}, "options" : "i" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input": "a", "regex" : "a", "options" : "g" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : null} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : 1} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input": "string", "regex" :"", "options" : "\u0000"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input": "string", "regex" :"\u0000", "options" : "i"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexMatch" : {"input": "string", "regex" :"(m(p)"} } }');

-- $regexFind operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input":"", "regex" : "" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input":"a", "regex" : "a" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input":"apple", "regex" : "apple" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input":"apple is a fruit", "regex" : "is" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input":"apple is a fruit", "regex" : "fruit" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input":"APPLE", "regex" : "apple" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input":"APPLE", "regex" : "apple", "options": "i" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input":"APPLE","regex" : {"$regex" : "apple", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"2"}', '{"result" : { "$regexFind" : {"input":"Banana","regex" : {"$regex" : "banana", "$options" : "i"} }} }');

-- $regexFind operator: complex regex pattern test:
select bson_dollar_project('{"_id":"4"}', '{"result" : { "$regexFind" : {"input":"1234567890","regex" : "\\d{3}", "options" : "" }} }');
select bson_dollar_project('{"_id":"3"}', '{"result" : { "$regexFind" : {"input":"Hello World","regex" : "\\bworld\\b", "options" : "i" }} }');
select bson_dollar_project('{"_id":"5"}', '{"result" : { "$regexFind" : {"input":"Microsoft IDC","regex" : "id", "options" : "i" }} }');
select bson_dollar_project('{"_id":"6"}', '{"result" : { "$regexFind" : {"input":"Hello, World!","regex" : "[,!]", "options" : "i" }} }');
select bson_dollar_project('{"_id":"7"}', '{"result" : { "$regexFind" : {"input":"Lorem ipsum dolor sit amet, consectetur adipiscing elit.","regex" : "\\b\\w{5}\\b", "options" : "i" }} }');
select bson_dollar_project('{"_id":"8"}', '{"result" : { "$regexFind" : {"input":"Email me at john@example.com or call 555-123-4567 for more information.","regex" : "\\b[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}\\b|\\b\\d{3}-\\d{3}-\\d{4}\\b", "options" : "i" }} }');
select bson_dollar_project('{"_id":"9"}', '{"result" : { "$regexFind" : {"input":"<h1>Hello</h1><p>Paragraph 1</p><p>Paragraph 2</p>","regex" : "<p>(.*?)</p>", "options" : "i" }} }');

-- $regexFind operator: complex regex pattern with BSON_TYPE_REGEX:
select bson_dollar_project('{"_id":"4"}', '{"result" : { "$regexFind" : {"input":"1234567890","regex" : {"$regex" : "\\d{3}", "$options" : ""} }} }');
select bson_dollar_project('{"_id":"3"}', '{"result" : { "$regexFind" : {"input":"Hello World","regex" : {"$regex" : "\\bworld\\b", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"5"}', '{"result" : { "$regexFind" : {"input":"Microsoft IDC","regex" : {"$regex" : "id", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"6"}', '{"result" : { "$regexFind" : {"input":"Hello, World!","regex" : {"$regex" : "[,!]", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"7"}', '{"result" : { "$regexFind" : {"input":"Lorem ipsum dolor sit amet, consectetur adipiscing elit.","regex" : {"$regex" : "\\b\\w{5}\\b", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"8"}', '{"result" : { "$regexFind" : {"input":"Email me at john@example.com or call 555-123-4567 for more information.","regex" : {"$regex" : "\\b[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}\\b|\\b\\d{3}-\\d{3}-\\d{4}\\b", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"9"}', '{"result" : { "$regexFind" : {"input":"<h1>Hello</h1><p>Paragraph 1</p><p>Paragraph 2</p>","regex" : {"$regex" : "<p>(.*?)</p>", "$options" : "i"} }} }');

-- $regexFind operator: captures in output:
select bson_dollar_project('{"_id":"10"}', '{"result" : { "$regexFind" : {"input":"John Doe, 25 years old","regex" : {"$regex" : "(\\w+) (\\w+), (\\d+) years old", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"11"}', '{"result" : { "$regexFind" : {"input":"Date: 2023-07-14","regex" : {"$regex" : "Date: (\\d{4}-\\d{2}-\\d{2})", "$options" : ""} }} }');
select bson_dollar_project('{"_id":"12"}', '{"result" : { "$regexFind" : {"input":"Product: Apple iPhone 12","regex" : {"$regex" : "Product: (\\w+) (\\w+) (\\d+)", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"13"}', '{"result" : { "$regexFind" : {"input":"Email: john@example.com","regex" : {"$regex" : "Email: ([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,})", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"14"}', '{"result" : { "$regexFind" : {"input":"Lorem ipsum dolor sit amet, consectetur adipiscing elit.","regex" : {"$regex" : "(\\b\\w{5}\\b)", "$options" : ""} }} }');

-- $regexFind operator: different options:
select bson_dollar_project('{"_id":"15"}', '{"result" : { "$regexFind" : {"input":"Hello\nworld","regex" : {"$regex" : "^world", "$options" : "m"} }} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexFind" : {"input":"The quick brown FOX","regex" : {"$regex" : "\\bfox\\b", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"17"}', '{"result" : { "$regexFind" : {"input":"123\n456\n789","regex" : {"$regex" : "\\d+", "$options" : "g"}}}}');
select bson_dollar_project('{"_id":"18"}', '{"result" : { "$regexFind" : {"input":"This is a long text","regex" : {"$regex" : "long text", "$options" : "is"} }} }');
select bson_dollar_project('{"_id":"20"}', '{"result" : { "$regexFind" : {"input":"Hello there","regex" : {"$regex" : "Hello\\s there", "$options" : "x"} }} }');

-- $regexFind operator: null cases:
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexFind" : {"input":null,"regex" : {"$regex" : "", "$options" : ""} }
} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexFind" : {"input":"a","regex" : null }} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexFind" : {"input":"$z","regex" : {"$regex" : "", "$options" : ""} }
} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexFind" : {"input":"a","regex" : "$z" }} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexFind" : {"input":"apple","regex" : "boy" }} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexFind" : {"input":"The quick brown FOX","regex" : {"$regex" : "\\bfox\\b", "$options" : ""} }} }');
select bson_dollar_project('{"_id":"19"}', '{"result" : { "$regexFind" : {"input":"This is a\nmultiline\nstring","regex" : {"$regex" : "multiline string", "$options" : "ix"} }} }');

-- $regexFind operator: expression cases:
select bson_dollar_project('{"_id":"1","a":"apple", "b":"app"}', '{"result" : { "$regexFind" : {"input":"$a", "regex" : "$b" }} }');
select bson_dollar_project('{"_id":"1","a":"APPLE", "b":"app", "c" : "i"}', '{"result" : { "$regexFind" : {"input":"$a", "regex" : "$b", "options" :"$c" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input": {"$concat" : ["APPLE", "IS", "FRUIT"]}, "regex" : {"$toLower" : "APPLE"}, "options": {"$concat" : "i"} }} }');

-- $regexFind operator: negative test cases :
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input":"", "regex" : "", "extra" : "" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : { "regex" : ""}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : { "input" : ""}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input": 1, "regex" : "", "options" : "" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input": "a", "regex" : 1, "options" : "" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input": "a", "regex" : "a", "options" : 1 }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input": "a", "regex" : {"$regex" : "", "$options": "i"}, "options" : "i" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input": "a", "regex" : "a", "options" : "g" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : null} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : 1} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input": "string", "regex" :"", "options" : "\u0000"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input": "string", "regex" :"\u0000", "options" : "i"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFind" : {"input": "string", "regex" :"(m(p)"} } }');

-- $regexFindAll operator: basic test:
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input":"", "regex" : "" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input":"a", "regex" : "a" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input":"apple", "regex" : "apple" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input":"apple is a fruit", "regex" : "is" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input":"apple is a fruit", "regex" : "fruit" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input":"APPLE", "regex" : "apple" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input":"APPLE", "regex" : "apple", "options": "i" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input":"APPLE","regex" : {"$regex" : "apple", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"2"}', '{"result" : { "$regexFindAll" : {"input":"Banana","regex" : {"$regex" : "banana", "$options" : "i"} }} }');

-- $regexFindAll operator: complex regex pattern test:
select bson_dollar_project('{"_id":"4"}', '{"result" : { "$regexFindAll" : {"input":"1234567890","regex" : "\\d{3}", "options" : "" }} }');
select bson_dollar_project('{"_id":"3"}', '{"result" : { "$regexFindAll" : {"input":"Hello World","regex" : "\\bworld\\b", "options" : "i" }} }');
select bson_dollar_project('{"_id":"5"}', '{"result" : { "$regexFindAll" : {"input":"Microsoft IDC","regex" : "id", "options" : "i" }} }');
select bson_dollar_project('{"_id":"6"}', '{"result" : { "$regexFindAll" : {"input":"Hello, World!","regex" : "[,!]", "options" : "i" }} }');
select bson_dollar_project('{"_id":"7"}', '{"result" : { "$regexFindAll" : {"input":"Lorem ipsum dolor sit amet, consectetur adipiscing elit.","regex" : "\\b\\w{5}\\b", "options" : "i" }} }');
select bson_dollar_project('{"_id":"8"}', '{"result" : { "$regexFindAll" : {"input":"Email me at john@example.com or call 555-123-4567 for more information.","regex" : "\\b[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}\\b|\\b\\d{3}-\\d{3}-\\d{4}\\b", "options" : "i" }} }');
select bson_dollar_project('{"_id":"9"}', '{"result" : { "$regexFindAll" : {"input":"<h1>Hello</h1><p>Paragraph 1</p><p>Paragraph 2</p>","regex" : "<p>(.*?)</p>", "options" : "i" }} }');
select bson_dollar_project('{"_id":"1"}', ('{"result" : { "$regexFindAll" : {"input" : "' || LPAD('', 100, 'a') || '", "regex": "((((((((((((((()))))))))))))))"}} }')::bson);
select bson_dollar_project('{"_id":"1"}', ('{"result" : { "$regexFindAll" : {"input" : "' || LPAD('', 50000, 'c') || LPAD('', 50000, 'd') ||  'e", "regex": "' || LPAD('',2500*3,'(d)') || 'e"}} }')::bson);

-- $regexFindAll operator: complex regex pattern with BSON_TYPE_REGEX:
select bson_dollar_project('{"_id":"4"}', '{"result" : { "$regexFindAll" : {"input":"1234567890","regex" : {"$regex" : "\\d{3}", "$options" : ""} }} }');
select bson_dollar_project('{"_id":"3"}', '{"result" : { "$regexFindAll" : {"input":"Hello World","regex" : {"$regex" : "\\bworld\\b", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"5"}', '{"result" : { "$regexFindAll" : {"input":"Microsoft IDC","regex" : {"$regex" : "id", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"6"}', '{"result" : { "$regexFindAll" : {"input":"Hello, World!","regex" : {"$regex" : "[,!]", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"7"}', '{"result" : { "$regexFindAll" : {"input":"Lorem ipsum dolor sit amet, consectetur adipiscing elit.","regex" : {"$regex" : "\\b\\w{5}\\b", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"8"}', '{"result" : { "$regexFindAll" : {"input":"Email me at john@example.com or call 555-123-4567 for more information.","regex" : {"$regex" : "\\b[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}\\b|\\b\\d{3}-\\d{3}-\\d{4}\\b", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"9"}', '{"result" : { "$regexFindAll" : {"input":"<h1>Hello</h1><p>Paragraph 1</p><p>Paragraph 2</p>","regex" : {"$regex" : "<p>(.*?)</p>", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input": "ABCDXYZABCDXYZABCDXYZ", "regex" : "BC(P(DE)?)?"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input": "aaa aa", "regex" : "(a*?)"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input": "aaaaa aaaa", "regex" : "(aa)"} } }');

-- $regexFindAll operator: captures in output:
select bson_dollar_project('{"_id":"10"}', '{"result" : { "$regexFindAll" : {"input":"John Doe, 25 years old","regex" : {"$regex" : "(\\w+) (\\w+), (\\d+) years old", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"11"}', '{"result" : { "$regexFindAll" : {"input":"Date: 2023-07-14","regex" : {"$regex" : "Date: (\\d{4}-\\d{2}-\\d{2})", "$options" : ""} }} }');
select bson_dollar_project('{"_id":"12"}', '{"result" : { "$regexFindAll" : {"input":"Product: Apple iPhone 12","regex" : {"$regex" : "Product: (\\w+) (\\w+) (\\d+)", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"13"}', '{"result" : { "$regexFindAll" : {"input":"Email: john@example.com","regex" : {"$regex" : "Email: ([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,})", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"14"}', '{"result" : { "$regexFindAll" : {"input":"Lorem ipsum dolor sit amet, consectetur adipiscing elit.","regex" : {"$regex" : "(\\b\\w{5}\\b)", "$options" : ""} }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input": "", "regex" : "(missing)|()"} } }');

-- $regexFindAll operator: different options:
select bson_dollar_project('{"_id":"15"}', '{"result" : { "$regexFindAll" : {"input":"Hello\nworld","regex" : {"$regex" : "^world", "$options" : "m"} }} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexFindAll" : {"input":"The quick brown FOX","regex" : {"$regex" : "\\bfox\\b", "$options" : "i"} }} }');
select bson_dollar_project('{"_id":"17"}', '{"result" : { "$regexFindAll" : {"input":"123\n456\n789","regex" : {"$regex" : "\\d+", "$options" : "g"}}}}');
select bson_dollar_project('{"_id":"18"}', '{"result" : { "$regexFindAll" : {"input":"This is a long text","regex" : {"$regex" : "long text", "$options" : "is"} }} }');
select bson_dollar_project('{"_id":"20"}', '{"result" : { "$regexFindAll" : {"input":"Hello there","regex" : {"$regex" : "Hello\\s there", "$options" : "x"} }} }');

-- $regexFindAll operator: empty cases:
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexFindAll" : {"input":null,"regex" : {"$regex" : "", "$options" : ""} }
} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexFindAll" : {"input":"a","regex" : null }} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexFindAll" : {"input":"$z","regex" : {"$regex" : "", "$options" : ""} }
} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexFindAll" : {"input":"a","regex" : "$z" }} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexFindAll" : {"input":"apple","regex" : "boy" }} }');
select bson_dollar_project('{"_id":"16"}', '{"result" : { "$regexFindAll" : {"input":"The quick brown FOX","regex" : {"$regex" : "\\bfox\\b", "$options" : ""} }} }');
select bson_dollar_project('{"_id":"19"}', '{"result" : { "$regexFindAll" : {"input":"This is a\nmultiline\nstring","regex" : {"$regex" : "multiline string", "$options" : "ix"} }} }');

-- $regexFindAll operator: expression cases:
select bson_dollar_project('{"_id":"1","a":"apple", "b":"app"}', '{"result" : { "$regexFindAll" : {"input":"$a", "regex" : "$b" }} }');
select bson_dollar_project('{"_id":"1","a":"APPLE", "b":"app", "c" : "i"}', '{"result" : { "$regexFindAll" : {"input":"$a", "regex" : "$b", "options" :"$c" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input": {"$concat" : ["APPLE", "IS", "FRUIT"]}, "regex" : {"$toLower" : "APPLE"}, "options": {"$concat" : "i"} }} }');

-- $regexFindAll operator: negative test cases :
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input":"", "regex" : "", "extra" : "" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : { "regex" : ""}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : { "input" : ""}} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input": 1, "regex" : "", "options" : "" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input": "a", "regex" : 1, "options" : "" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input": "a", "regex" : "a", "options" : 1 }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input": "a", "regex" : {"$regex" : "", "$options": "i"}, "options" : "i" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input": "a", "regex" : "a", "options" : "g" }} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : null} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : 1} }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input": "string", "regex" :"", "options" : "\u0000"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input": "string", "regex" :"\u0000", "options" : "i"} } }');
select bson_dollar_project('{"_id":"1"}', '{"result" : { "$regexFindAll" : {"input": "string", "regex" :"(m(p)"} } }');
select bson_dollar_project('{"_id":"1"}', ('{"result" : { "$regexFindAll" : {"input" : "' || LPAD('',100000, 'a') || '", "regex": "(((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((())))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))"}} }')::bson);
select bson_dollar_project('{"_id":"1"}', ('{"result" : { "$regexFindAll" : {"input" : "' || LPAD('', 50000, 'c') || LPAD('', 50000, 'd') ||  'e", "regex": "' || LPAD('',2728*3,'(d)') || 'e"}} }')::bson);

--regex Operators : with multiple document in collection

--creating collection
SELECT create_collection('db','regexMultiDocumentTest');

--inserting data
SELECT documentdb_api.insert_one('db','regexMultiDocumentTest','{"_id":1, "input" : "apple", "regex" : "apple", "option" : "i"}', NULL);
SELECT documentdb_api.insert_one('db','regexMultiDocumentTest','{"_id":2, "input" : "apple is sweet", "regex" : "apple", "option" : "i"}', NULL);
SELECT documentdb_api.insert_one('db','regexMultiDocumentTest','{"_id":3, "input" : "apple is lime", "regex" : "apple", "option" : "i"}', NULL);
SELECT documentdb_api.insert_one('db','regexMultiDocumentTest','{"_id":4, "input" : "lime is blue", "regex" : "apple", "option" : "i"}', NULL);
SELECT documentdb_api.insert_one('db','regexMultiDocumentTest','{"_id":5, "input" : "blue is berry", "regex" : "apple", "option" : "i"}', NULL);
SELECT documentdb_api.insert_one('db','regexMultiDocumentTest','{"_id":6, "input" : "need apple for nothing", "regex" : "apple", "option" : "i"}', NULL);
SELECT documentdb_api.insert_one('db','regexMultiDocumentTest','{"_id":7, "input" : "One apple a day keeps doctor away", "regex" : "apple", "option" : "i"}', NULL);
SELECT documentdb_api.insert_one('db','regexMultiDocumentTest','{"_id":8, "input" : "red apple is red ?", "regex" : "apple", "option" : "i"}', NULL);

--case 1: when regex and options are constant  we compile regex only once
SELECT bson_dollar_project(document, '{"result" : { "$regexMatch" : {"input" : "$input", "regex": "apple", "options" : "i"} }}') from documentdb_api.collection('db','regexMultiDocumentTest');
SELECT bson_dollar_project(document, '{"result" : { "$regexFind" : {"input" : "$input", "regex": "apple", "options" : "i"} }}') from documentdb_api.collection('db','regexMultiDocumentTest');
SELECT bson_dollar_project(document, '{"result" : { "$regexFindAll" : {"input" : "$input", "regex": "apple", "options" : "i"} }}') from documentdb_api.collection('db','regexMultiDocumentTest');

--case 2: when regex and options are not constant  we compile regex every document
SELECT bson_dollar_project(document, '{"result" : { "$regexMatch" : {"input" : "$input", "regex": "$regex", "options" : "$option"} }}') from documentdb_api.collection('db','regexMultiDocumentTest');
SELECT bson_dollar_project(document, '{"result" : { "$regexFind" : {"input" : "$input", "regex": "$regex", "options" : "$option"} }}') from documentdb_api.collection('db','regexMultiDocumentTest');
SELECT bson_dollar_project(document, '{"result" : { "$regexFindAll" : {"input" : "$input", "regex": "$regex", "options" : "$option"} }}') from documentdb_api.collection('db','regexMultiDocumentTest');

--dropping collection
SELECT drop_collection('db','regexMultiDocumentTest');
-- $replaceOne basic tests
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "blue paint", "find": "blue paint", "replacement": "red paint"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "blue paint", "find": "blue and green paint", "replacement": "red paint"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "blue paint with blue paintbrush", "find": "blue paint", "replacement": "red paint"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "blue paint with green paintbrush", "find": "blue paint", "replacement": "red paint"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "albatross", "find": "ross", "replacement": "rachel"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "albatross", "find": "", "replacement": "one"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "albatross", "find": "", "replacement": ""}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "", "find": "", "replacement": "foo"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "albatross", "find": "rachel", "replacement": "ross"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "", "find": "rachel", "replacement": "ross"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "", "find": "rachel", "replacement": ""}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "albatross", "find": "", "replacement": ""}}}');

-- $replaceOne null tests
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": null, "find": "ross", "replacement": "rachel"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "albatross", "find": null, "replacement": "yelo"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "albatross", "find": "", "replacement": null}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "albatross", "find": null, "replacement": null}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": null, "find": "f", "replacement": null}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": null, "find": null, "replacement": "chandler"}}}');

-- $replaceOne with path
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceOne": {"input": "$test", "find": "el", "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceOne": {"input": "$test.nest", "find": "el", "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceOne": {"input": "wandering hellostater", "find": "$test.nest", "replacement": "phoebe"}}}');
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceOne": {"input": "wandering cheapskate", "find": "cheap", "replacement": "$test.nest"}}}');
select bson_dollar_project('{"_id":"1", "test": {"input": "hello", "find": "ello", replacement: "atred"}}', '{"result": {"$replaceOne": {"input": "$input", "find": "$find", "replacement": "$replacement"}}}');

-- $replaceOne with missing path
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceOne": {"input": "$test.nesty", "find": "el", "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceOne": {"input": "Present monsieur", "find": "$madame", "replacement": "non"}}}');
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceOne": {"input": "Present monsieur", "find": "si", "replacement": "$non"}}}');

-- $replaceOne unsupported data types
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceOne": {"input": 1, "find": "el", "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceOne": {"input": "$test", "find": 2, "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceOne": {"input": "yellow", "find": "el", "replacement": 3}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceOne": {"input": ["\u0022\u00A9\u0022\u00A9\u0022\u00A9\u0022\u00A9", 1, 2], "find": "el", "replacement": 3}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceOne": {"input": "yellow", "find": {"$binary": {"base64": "ww==", "subType": "01"}}, "replacement": 3}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceOne": {"input": "yellow", "find": "el", "replacement": {"$regex": "/ab/cdsd/abc", "$options" : ""}}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceOne": {"input": "$test", "find": NaN, "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": {"a": "this is a string"}}', '{"result": {"$replaceOne": {"input": "$test", "find": "is", "replacement": "chandler"}}}');

-- $replaceOne missing/additional argument
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceOne": {"find": NaN, "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceOne": {"input": "$test", "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceOne": {"input": "$test", "find": "asdf"}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceOne": {"input": "$test"}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceOne": {"replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceOne": {"find": NaN}}}');
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceOne": {"input": "Present monsieur", "find": "si", "replacement": "$non", "newField": "he"}}}');
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceOne": {"input": "Present monsieur", "find": "si", "newField": "he"}}}');

-- $replaceOne operator: special characters and escape sequences test:
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "I ❤️ documentdb", "find": "❤️", "replacement": "\u0000B"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "jalapeño", "find": "peñ", "replacement": "寿司"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "a\u0000B\u000d", "find": "\u000d", "replacement": "🥲"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "cafétéria", "find": "caf", "replacement": "é"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceOne": {"input": "cafe\u0301", "find": "café", "replacement": "CAFE" }}}');


-- $replaceAll basic tests
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "blue paint", "find": "blue paint", "replacement": "red paint"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "foo bar", "find": "o", "replacement": ""}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "foo bar", "find": "o", "replacement": "O"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "foo bar", "find": "o", "replacement": "OO"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "foooo bar", "find": "o", "replacement": "O"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "blue paint", "find": "blue and green paint", "replacement": "red paint"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "blue paint with blue paintbrush", "find": "blue paint", "replacement": "red paint"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "blue paint with green paintbrush", "find": "blue paint", "replacement": "red paint"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "albatross", "find": "ross", "replacement": "rachel"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "albatross", "find": "", "replacement": "one"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "albatross", "find": "", "replacement": ""}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "", "find": "", "replacement": "foo"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "albatross", "find": "rachel", "replacement": "ross"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "", "find": "rachel", "replacement": "ross"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "", "find": "rachel", "replacement": ""}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "albatross", "find": "", "replacement": ""}}}');

-- $replaceAll null tests
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": null, "find": "ross", "replacement": "rachel"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "albatross", "find": null, "replacement": "yelo"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "albatross", "find": "", "replacement": null}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "albatross", "find": null, "replacement": null}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": null, "find": "f", "replacement": null}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": null, "find": null, "replacement": "chandler"}}}');

-- $replaceAll with path
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceAll": {"input": "$test", "find": "el", "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceAll": {"input": "$test.nest", "find": "el", "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceAll": {"input": "wandering hellostater", "find": "$test.nest", "replacement": "phoebe"}}}');
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceAll": {"input": "wandering cheapskate", "find": "cheap", "replacement": "$test.nest"}}}');

-- $replaceAll with missing path
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceAll": {"input": "$test.nesty", "find": "el", "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceAll": {"input": "Present monsieur", "find": "$madame", "replacement": "non"}}}');
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceAll": {"input": "Present monsieur", "find": "si", "replacement": "$non"}}}');

-- $replaceAll unsupported data types
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceAll": {"input": 1, "find": "el", "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceAll": {"input": "$test", "find": 2, "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceAll": {"input": "yellow", "find": "el", "replacement": 3}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceAll": {"input": ["\u0022\u00A9\u0022\u00A9\u0022\u00A9\u0022\u00A9", 1, 2], "find": "el", "replacement": 3}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceAll": {"input": "yellow", "find": {"$binary": {"base64": "ww==", "subType": "01"}}, "replacement": 3}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceAll": {"input": "yellow", "find": "el", "replacement": {"$regex": "/ab/cdsd/abc", "$options" : ""}}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceAll": {"input": "$test", "find": NaN, "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": {"a": "this is a string"}}', '{"result": {"$replaceAll": {"input": "$test", "find": "is", "replacement": "chandler"}}}');

-- $replaceAll missing/additional argument
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceAll": {"find": NaN, "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceAll": {"input": "$test", "replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceAll": {"input": "$test", "find": "asdf"}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceAll": {"input": "$test"}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceAll": {"replacement": "chandler"}}}');
select bson_dollar_project('{"_id":"1", "test": "hello"}', '{"result": {"$replaceAll": {"find": NaN}}}');
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceAll": {"input": "Present monsieur", "find": "si", "replacement": "$non", "newField": "he"}}}');
select bson_dollar_project('{"_id":"1", "test": {"nest": "hello"}}', '{"result": {"$replaceAll": {"input": "Present monsieur", "find": "si", "newField": "he"}}}');

-- $replaceAll operator: special characters and escape sequences test:
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "I ❤️ documentdb", "find": "❤️", "replacement": "\u0000B"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "jalapeño", "find": "peñ", "replacement": "寿司"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "a\u0000B\u000d", "find": "\u000d", "replacement": "🥲"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "cafétéria", "find": "caf", "replacement": "é"}}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "cafe\u0301", "find": "café", "replacement": "CAFE" }}}');

-- $replaceAll multiple replacements
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "The quick fox jumped over the laze dog", "find": "d", "replacement": "cat" }}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "albatross and its wings", "find": " ", "replacement": "huge" }}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "blue paint with blue paintbrush", "find": "blue paint", "replacement": "green paint" }}}');
select bson_dollar_project('{"_id":"1"}', '{"result": {"$replaceAll": {"input": "yellow", "find": "", "replacement": "z" }}}');
