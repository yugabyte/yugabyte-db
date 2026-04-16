### Perform aggregations `Group by`

DocumentDB provides the [documentdb_api.aggregate_cursor_first_page](https://github.com/microsoft/documentdb/wiki/Functions#aggregate_cursor_first_page) function, for performing aggregations over the document store.

The example projects an aggregation on number of patients registered over the years.

```sql
SELECT cursorpage FROM documentdb_api.aggregate_cursor_first_page('documentdb', '{ "aggregate": "patient", "pipeline": [ { "$group": { "_id": "$registration_year", "count_patients": { "$count": {} } } } ] , "cursor": { "batchSize": 3 } }');
```

We can perform more complex operations, listing below a few more usage examples.
The example demonstrates an aggregation on patients, categorizing them into buckets defined by registration_year boundaries.

```sql
SELECT cursorpage FROM documentdb_api.aggregate_cursor_first_page('documentdb', '{ "aggregate": "patient", "pipeline": [ { "$bucket": { "groupBy": "$registration_year", "boundaries": ["2022","2023","2024"], "default": "unknown" } } ], "cursor": { "batchSize": 3 } }');
```

This query performs an aggregation on the `patient` collection to group documents by `registration_year`. It collects unique patient conditions for each registration year using the `$addToSet` operator.

```sql
SELECT cursorpage FROM documentdb_api.aggregate_cursor_first_page('documentdb', '{ "aggregate": "patient", "pipeline": [ { "$group": { "_id": "$registration_year", "conditions": { "$addToSet": { "conditions" : "$conditions" } } } } ], "cursor": { "batchSize": 3 } }');
```
