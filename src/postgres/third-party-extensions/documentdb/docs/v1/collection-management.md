### Collection management

We can review for the available collections and databases by querying [documentdb_api.list_collections_cursor_first_page](https://github.com/microsoft/documentdb/wiki/Functions#list_collections_cursor_first_page).

```sql
SELECT * FROM documentdb_api.list_collections_cursor_first_page('documentdb', '{ "listCollections": 1 }');
```

[documentdb_api.list_indexes_cursor_first_page](https://github.com/microsoft/documentdb/wiki/Functions#list_indexes_cursor_first_page) allows reviewing for the existing indexes on a collection. We can find collection_id from `documentdb_api.list_collections_cursor_first_page`.

```sql
SELECT documentdb_api.list_indexes_cursor_first_page('documentdb','{"listIndexes": "patient"}');
```

`ttl` indexes by default gets scheduled through the `pg_cron` scheduler, which could be reviewed by querying the `cron.job` table.

```sql
select * from cron.job;
```
