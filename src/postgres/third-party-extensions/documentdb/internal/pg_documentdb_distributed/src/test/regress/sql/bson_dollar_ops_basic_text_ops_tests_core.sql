
SELECT documentdb_api.insert_one('db','queryregexopstest', '{"_id": 1, "address" : "sfo0001", "comment" : "This     is a dentist     clinic"}', NULL);
SELECT documentdb_api.insert_one('db','queryregexopstest', '{"_id": 2, "address" : "sfo0010", "comment" : "Here you can get\nthe most delicious food\nin the world"}', NULL);
SELECT documentdb_api.insert_one('db','queryregexopstest', '{"_id": 3, "address" : "Sfo0010", "comment" : "[dupe] Here you can get\nthe most delicious food\nin the world"}', NULL);
SELECT documentdb_api.insert_one('db','queryregexopstest', '{"_id": 4, "address" : "la0001", "comment" : "I never been here"}', NULL);
SELECT documentdb_api.insert_one('db','queryregexopstest', '{"_id": 5,  "address" : "la7777", "comment" : "The dog in the yard\nalways barks at me" }', NULL);

/* now query regex */
/* db.queryregexopstest.find( { address: { $regex: /0001$/ } } ) */
SELECT object_id, document FROM documentdb_api.collection('db','queryregexopstest') WHERE document @~ '{ "address": "0001$" }';

/* TODO: passing options: db.queryregexopstest.find( { address: { $regex: /^SFO/i } } ) */
SELECT object_id, document FROM documentdb_api.collection('db','queryregexopstest') WHERE document @~ '{ "address": "^sfo" }';

/* Match multiple spaces */
SELECT object_id, document FROM documentdb_api.collection('db','queryregexopstest') WHERE document @~ '{ "comment": "\\s\\s+" }';

