
### Indexing

#### Create an Index

DocumentDB uses the `documentdb_api.create_indexes_background` function, which allows background index creation without disrupting database operations.

The SQL command demonstrates how to create a `single field` index on `age` on the `patient` collection of the `documentdb`.

```sql
SELECT * FROM documentdb_api.create_indexes_background('documentdb', '{ "createIndexes": "patient", "indexes": [{ "key": {"age": 1},"name": "idx_age"}]}');
```

The SQL command demonstrates how to create a `compound index` on fields age and registration_year on the `patient` collection of the `documentdb`.

```sql
SELECT * FROM documentdb_api.create_indexes_background('documentdb', '{ "createIndexes": "patient", "indexes": [{ "key": {"registration_year": 1, "age": 1},"name": "idx_regyr_age"}]}');
```

#### Drop an Index

DocumentDB uses the `documentdb_api.drop_indexes` function, which allows you to remove an existing index from a collection. The SQL command demonstrates how to drop the index named `id_ab_1` from the `first_collection` collection of the `documentdb`.

```sql
CALL documentdb_api.drop_indexes('documentdb', '{"dropIndexes": "patient", "index":"idx_age"}');
```