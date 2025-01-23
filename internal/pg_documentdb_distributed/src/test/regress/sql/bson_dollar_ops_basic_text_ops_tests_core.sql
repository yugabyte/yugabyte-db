
/* Insert documents based off the mongo help example for $regex */
SELECT helio_api.insert_one('db','queryregexopstest', '{"_id": 1, "sku" : "abc123", "description" : "Single line description."}', NULL);
SELECT helio_api.insert_one('db','queryregexopstest', '{"_id": 2, "sku" : "abc789", "description" : "First line\nSecond line"}', NULL);
SELECT helio_api.insert_one('db','queryregexopstest', '{"_id": 3, "sku" : "xyz456", "description" : "Many spaces before     line"}', NULL);
SELECT helio_api.insert_one('db','queryregexopstest', '{"_id": 4,  "sku" : "xyz789", "description" : "Multiple\nline description" }', NULL);

/* now query regex */
/* db.products.find( { sku: { $regex: /789$/ } } ) */
SELECT object_id, document FROM helio_api.collection('db','queryregexopstest') WHERE document @~ '{ "sku": "789$" }';

/* TODO: passing options: db.products.find( { sku: { $regex: /^ABC/i } } ) */
SELECT object_id, document FROM helio_api.collection('db','queryregexopstest') WHERE document @~ '{ "sku": "^abc" }';

/* Match multiple spaces */
SELECT object_id, document FROM helio_api.collection('db','queryregexopstest') WHERE document @~ '{ "description": "\\s\\s+" }';

