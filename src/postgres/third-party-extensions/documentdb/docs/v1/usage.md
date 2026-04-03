
## Usage

Once you have your `DocumentDB` set up running, you can start with creating collections, indexes and perform queries on them.

### Create a collection

DocumentDB provides [documentdb_api.create_collection](https://github.com/microsoft/documentdb/wiki/Functions#create_collection) function to create a new collection within a specified database, enabling you to manage and organize your BSON documents effectively.

```sql
SELECT documentdb_api.create_collection('documentdb','patient');
```

### Perform CRUD operations

#### Insert documents

The [documentdb_api.insert_one](https://github.com/microsoft/documentdb/wiki/Functions#insert_one) command is used to add a single document into a collection.

```sql
select documentdb_api.insert_one('documentdb','patient', '{ "patient_id": "P001", "name": "Alice Smith", "age": 30, "phone_number": "555-0123", "registration_year": "2023","conditions": ["Diabetes", "Hypertension"]}');
select documentdb_api.insert_one('documentdb','patient', '{ "patient_id": "P002", "name": "Bob Johnson", "age": 45, "phone_number": "555-0456", "registration_year": "2023", "conditions": ["Asthma"]}');
select documentdb_api.insert_one('documentdb','patient', '{ "patient_id": "P003", "name": "Charlie Brown", "age": 29, "phone_number": "555-0789", "registration_year": "2024", "conditions": ["Allergy", "Anemia"]}');
select documentdb_api.insert_one('documentdb','patient', '{ "patient_id": "P004", "name": "Diana Prince", "age": 40, "phone_number": "555-0987", "registration_year": "2024", "conditions": ["Migraine"]}');
select documentdb_api.insert_one('documentdb','patient', '{ "patient_id": "P005", "name": "Edward Norton", "age": 55, "phone_number": "555-1111", "registration_year": "2025", "conditions": ["Hypertension", "Heart Disease"]}');
```

#### Read document from a collection

The `documentdb_api.collection` function is used for retrieving the documents in a collection.

```sql
SELECT document FROM documentdb_api.collection('documentdb','patient');
```

Alternatively, we can apply filter to our queries.

```sql
SET search_path TO documentdb_api, documentdb_core;
SET documentdb_core.bsonUseEJson TO true;

SELECT cursorPage FROM documentdb_api.find_cursor_first_page('documentdb', '{ "find" : "patient", "filter" : {"patient_id":"P005"}}');
```

We can perform range queries as well.

```sql
SELECT cursorPage FROM documentdb_api.find_cursor_first_page('documentdb', '{ "find" : "patient", "filter" : { "$and": [{ "age": { "$gte": 10 } },{ "age": { "$lte": 35 } }] }}');
```

#### Update document in a collection

DocumentDB uses the [documentdb_api.update](https://github.com/microsoft/documentdb/wiki/Functions#update) function to modify existing documents within a collection.

The SQL command updates the `age` for patient `P004`.

```sql
select documentdb_api.update('documentdb', '{"update":"patient", "updates":[{"q":{"patient_id":"P004"},"u":{"$set":{"age":14}}}]}');
```

Similarly, we can update multiple documents using `multi` property.

```sql
SELECT documentdb_api.update('documentdb', '{"update":"patient", "updates":[{"q":{},"u":{"$set":{"age":24}},"multi":true}]}');
```

#### Delete document from the collection

DocumentDB uses the [documentdb_api.delete](https://github.com/microsoft/documentdb/wiki/Functions#delete) function for precise document removal based on specified criteria.

The SQL command deletes the document for patient `P002`.

```sql
SELECT documentdb_api.delete('documentdb', '{"delete": "patient", "deletes": [{"q": {"patient_id": "P002"}, "limit": 1}]}');
```