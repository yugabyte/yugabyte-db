
# A simple Change Data Capture ([CDC]) extension.

This extension will extract the [DML] changes from Postgres WAL
using [Logical Decoding] and export them in JSON format using [serde].

## Principles

* Postgres triggers various callbacks at the different stages of a transaction
* The decoder defines some of these callbacks: begin, change, commit, etc.
* The callbacks extract the changes made during the transaction
* They build Rust structs (Action, Tuple) to represent those changes
* The structs are then serialized into JSON
* The JSON output is sent into a logical replication slot (i.e. a queue)
* The output can be consumed in various ways by a remote client

## Requirements

In order to use this extension with a cargo-pgrx managed instance, you'll
need to add the configuration below in "$PGRX_HOME/data-$PGVER/postgresql.conf".

``` ini
shared_preload_libraries = 'wal_decoder'
wal_level = logical
```

## Example

1- Create a table and publish it

``` sql
CREATE TABLE person (name TEXT, age INT);
ALTER TABLE person REPLICA IDENTITY FULL;
CREATE PUBLICATION gotham_pub FOR TABLE person;
```

2- Create a replication slot fed by the decoder

``` sql
SELECT pg_create_logical_replication_slot('gotham_slot', 'wal_decoder');
```

3- Consume the changes from the replication slot

``` sql
INSERT INTO person
VALUES ('Bruce Wayne',42),('Clark Kent',33);
```

``` sql
SELECT * FROM pg_logical_slot_get_changes('gotham_slot', NULL, NULL);

    lsn    | xid |                                     data
-----------+-----+------------------------------------------------------------------------------
 0/16A87C8 | 581 | {"typ":"BEGIN"}
 0/16A87C8 | 581 | {"typ":"INSERT","rel":"public.person","new":{"name":"Bruce Wayne","age":42}}
 0/16A8810 | 581 | {"typ":"INSERT","rel":"public.person","new":{"name":"Clark Kent","age":33}}
 0/16A8888 | 581 | {"typ":"COMMIT","committed":779145498360779,"change_count":2}
```

``` sql
UPDATE person SET name = 'Batman' WHERE name= 'Bruce Wayne';
```

``` sql
SELECT xid, jsonb_pretty(data::JSONB)
FROM pg_logical_slot_get_changes('gotham_slot', NULL, NULL);

 xid |           jsonb_pretty
-----+-----------------------------------
 587 | {                                +
     |     "typ": "BEGIN"               +
     | }
 587 | {                                +
     |     "new": {                     +
     |         "age": 42,               +
     |         "name": "Batman"         +
     |     },                           +
     |     "old": {                     +
     |         "age": 42,               +
     |         "name": "Bruce Wayne"    +
     |     },                           +
     |     "rel": "public.person",      +
     |     "typ": "UPDATE"              +
     | }
 587 | {                                +
     |     "typ": "COMMIT",             +
     |     "committed": 779179731927669,+
     |     "change_count": 1            +
     | }
```

## Limitations

This decoder is designed as a basic example and it has the following limitations:

* Only the REPLICA IDENTITY FULL mode is fully supported. Supporting REPLICA IDENTITY DEFAULT
  would require additional work.

* Only TEXT and INT values are serialized. Supporting other types should be trivial.


## Other WAL decoders

Here are some other implementations in C that can be useful:

* <https://github.com/dalibo/hackingpg/blob/main/journee5/audit/plugin_audit.c>
* <https://github.com/leptonix/decoding-json/blob/master/decoding_json.c>
* <https://github.com/michaelpq/pg_plugins/blob/main/decoder_raw/decoder_raw.c>
* <https://github.com/eulerto/wal2json/blob/master/wal2json.c>

<!-- Links -->

[CDC]: https://en.wikipedia.org/wiki/Change_data_capture
[DML]: https://en.wikipedia.org/wiki/Data_manipulation_language
[Logical Decoding]: https://www.postgresql.org/docs/current/logicaldecoding-explanation.html
[serde]: https://serde.rs

