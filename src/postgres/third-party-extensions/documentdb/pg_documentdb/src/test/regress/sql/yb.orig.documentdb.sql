SET search_path TO documentdb_core,documentdb_api,documentdb_api_catalog,documentdb_api_internal, public;
SET documentdb_core.bsonUseEJson TO true;

---- Test some basic bson functions ----
SELECT documentdb_api_catalog.bson_array_agg('{"a":1}'::bson, 'b');

---- Test basic api behavior ----

-- patient collection
SELECT documentdb_api.insert_one('documentdb','patient',
    '{ "_id":"1", "patient_id": "P001", "name": "Alice Smith", "age": 30,      "phone_number": "555-0123", "registration_year": "2023","conditions": ["Diabetes", "Hypertension"]}');
SELECT documentdb_api.insert_one('documentdb','patient',
    '{ "_id":"2", "patient_id": "P002", "name": "Bob Johnson", "age": 45,
     "phone_number": "555-0456", "registration_year": "2023", "conditions": ["Asthma"]}');
SELECT documentdb_api.insert_one('documentdb','patient',
    '{ "_id":"1", "patient_id": "P003", "name": "Charlie Brown", "age": 29,
     "phone_number": "555-0789", "registration_year": "2024", "conditions": ["Allergy", "Anemia"]}');
SELECT documentdb_api.insert_one('documentdb','patient',
    '{ "_id":"3", "patient_id": "P004", "name": "Diana Prince", "age": 40,
     "phone_number": "555-0987", "registration_year": "2024", "conditions": ["Migraine"]}');
SELECT documentdb_api.insert_one('documentdb','patient',
    '{ "_id":"4", "patient_id": "P005", "name": "Edward Norton", "age": 55,
     "phone_number": "555-1111", "registration_year": "2025", "conditions": ["Hypertension", "Heart Disease"]}');

-- Read the collection
SELECT document FROM documentdb_api.collection('documentdb','patient') ORDER BY object_id;;

-- Find with filter
SELECT cursorPage FROM documentdb_api.find_cursor_first_page('documentdb',
    '{ "find" : "patient", "filter" : {"patient_id":"P005"}}');

-- Range query
SELECT cursorPage FROM documentdb_api.find_cursor_first_page('documentdb',
    '{ "find" : "patient",
     "filter" : { "$and": [{ "age": { "$gte": 10 } },{ "age": { "$lte": 35 } }] }}');

-- Update one
SELECT documentdb_api.update('documentdb', '{"update":"patient",
    "updates":[{"q":{"patient_id":"P004"},"u":{"$set":{"age":14}}}]}');

-- Update many
SELECT documentdb_api.update('documentdb', '{"update":"patient",
    "updates":[{"q":{},"u":{"$set":{"age":24}},"multi":true}]}');

-- Delete one
SELECT documentdb_api.delete('documentdb', '{"delete": "patient",
    "deletes": [{"q": {"patient_id": "P002"}, "limit": 1}]}');

SELECT document FROM documentdb_api.collection('documentdb','patient') ORDER BY object_id;;

-- salaries collection
SELECT documentdb_api.insert('documentdb', '{"insert":"salaries", "documents":[
   { "_id" : 1, "employee": "Ant", "salary": 100000, "fiscal_year": 2017 },
   { "_id" : 2, "employee": "Bee", "salary": 120000, "fiscal_year": 2017 },
   { "_id" : 3, "employee": "Ant", "salary": 115000, "fiscal_year": 2018 },
   { "_id" : 4, "employee": "Bee", "salary": 145000, "fiscal_year": 2018 },
   { "_id" : 5, "employee": "Cat", "salary": 135000, "fiscal_year": 2018 },
   { "_id" : 6, "employee": "Cat", "salary": 150000, "fiscal_year": 2019 }
]}');

SELECT * FROM documentdb_api.list_collections_cursor_first_page('documentdb',
    '{ "listCollections": 1, "nameOnly": true }');
