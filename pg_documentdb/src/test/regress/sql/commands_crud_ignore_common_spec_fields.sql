-- These tests make sure that we ignore the common spec fields/actions that are not implemented for various commands

SET search_path TO helio_api,helio_core,helio_api_catalog;
SET citus.next_shard_id TO 160000;
SET helio_api.next_collection_id TO 1600;
SET helio_api.next_collection_index_id TO 1600;

-- create index tests
SELECT helio_api_internal.create_indexes_non_concurrently('db',
    '{
        "createIndexes": "ignoreCommonSpec",
        "indexes": [
            {
                "key": {"$**": 1}, "name": "idx_1",
                "wildcardProjection": {"a.b": 1, "a.b": {"c.d": 1}}
            }
        ],
		"commitQuorum" : 100,
		"writeConcern": { "w": "majority", "wtimeout": 5000 },
		"apiVersion": 1,
		"$db" : "tetsts",
		"db": "test2"
    }'
);

-- insert tests
select helio_api.insert('db', '{
	"insert":"ignoreCommonSpec", 
	"documents":[{"_id":99,"a":99}], "ordered": false,
        "writeConcern": { "w": "majority", "wtimeout": 5000 },
	"bypassDocumentValidation": true, 
	"comment": "NoOp"
	}');
select helio_api.insert('db', '{
	"insert":"ignoreCommonSpec", 
	"documents":[{"_id":21,"a":99}], 
	"ordered": false,
        "bypassDocumentValidation": true, 
	"comment": "NoOp2",
	"apiVersion": 1
	}');

-- delete tests
select helio_api.delete('db', '{
	"delete":"ignoreCommonSpec", 
	"deletes":[{"q":{},"limit":0}], 
	"let": { "a": 1}, 
	"writeConcern": { "w": "majority", "wtimeout": 5000 },
	"apiVersion": 1
	}');

-- query 
SELECT helio_api_catalog.bson_dollar_project(document, '{ "a" : 1 }') FROM helio_api.collection('db', 'ignoreCommonSpec') ORDER BY object_id;

-- insert again
select helio_api.insert('db', '{
	"insert":"ignoreCommonSpec", 
	"documents":[{"_id":1,"a":"id1"}], 
	"ordered": false,
        "bypassDocumentValidation": true, 
	"comment": "NoOp1",
	"apiVersion": 1
	}');
select helio_api.insert('db', '{
	"insert":"ignoreCommonSpec", 
	"documents":[{"_id":2,"a":"id2"}],
	"ordered": false,
        "bypassDocumentValidation": true,
	"comment": "NoOp2"}');

-- update tests
select helio_api.update('db', '{
	"update":"ignoreCommonSpec",
	"updates":[{"q":{"_id":10,"b":{"$ne":0}},"u":{"$set":{"b":0}},"multi":false}],
	"let": {"a" : 1},
	"writeConcern": { "w": "majority", "wtimeout": 5000 },
	"apiVersion": 1
	}');

-- quey 
SELECT helio_api_catalog.bson_dollar_project(document, '{ "a" : 1 }') FROM helio_api.collection('db', 'ignoreCommonSpec') ORDER BY object_id;