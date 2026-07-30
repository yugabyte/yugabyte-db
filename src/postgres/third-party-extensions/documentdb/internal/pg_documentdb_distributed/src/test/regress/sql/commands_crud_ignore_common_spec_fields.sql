-- These tests make sure that we ignore the common spec fields/actions that are not implemented for various commands

SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog;
SET citus.next_shard_id TO 160000;
SET documentdb.next_collection_id TO 1600;
SET documentdb.next_collection_index_id TO 1600;

-- insert tests
select documentdb_api.insert('db', '{
	"insert":"ignoreCommonSpec", 
	"documents":[{"_id":99,"a":99}], "ordered": false,
        "writeConcern": { "w": "majority", "wtimeout": 5000 },
	"bypassDocumentValidation": true, 
	"comment": "NoOp"
	}');
select documentdb_api.insert('db', '{
	"insert":"ignoreCommonSpec", 
	"documents":[{"_id":21,"a":99}], 
	"ordered": false,
        "bypassDocumentValidation": true, 
	"comment": "NoOp2",
	"apiVersion": 1
	}');

-- insert again
select documentdb_api.insert('db', '{
	"insert":"ignoreCommonSpec", 
	"documents":[{"_id":1,"a":"id1"}], 
	"ordered": false,
        "bypassDocumentValidation": true, 
	"comment": "NoOp1",
	"apiVersion": 1
	}');
select documentdb_api.insert('db', '{
	"insert":"ignoreCommonSpec", 
	"documents":[{"_id":2,"a":"id2"}],
	"ordered": false,
        "bypassDocumentValidation": true,
	"comment": "NoOp2"}');

-- create index tests
SELECT documentdb_api_internal.create_indexes_non_concurrently('db', '{
	"createIndexes": "ignoreCommonSpec", 
	"indexes": [{"key": {"a$**foo": 1}, "name": "my_idx_ignore"}],
	"commitQuorum" : 100,
	"writeConcern": { "w": "majority", "wtimeout": 5000 },
	"apiVersion": 1,
	"$db" : "tetsts",
	"db": "test2"	
	}', true);

-- quey 
SELECT bson_dollar_project(document, '{ "a" : 1 }') FROM documentdb_api.collection('db', 'ignoreCommonSpec') ORDER BY object_id;

-- drop index tests
CALL documentdb_api.drop_indexes('db', '{
	"dropIndexes": "ignoreCommonSpec", 
	"index":[],
	"writeConcern": { "w": "majority", "wtimeout": 5000 },
	"comment": "NoOp1",
	"apiVersion": 1
	}');

-- query
SELECT bson_dollar_project(document, '{ "a" : 1 }') FROM documentdb_api.collection('db', 'ignoreCommonSpec') ORDER BY object_id;
