# Catalog entities (`master-catalog-entity`)

Sys-catalog rows by `SysRowEntryType`, mirroring `CATALOG_ENTITY_TYPE_MAP`
(`src/yb/master/catalog_entity_types.h`), and the in-memory entity the catalog holds.

## Rows

`parse_entity` and `reserialize_entity` parse a row with the C++ runtime's semantics and serialize
it back byte for byte, keeping unknown fields ([proto.md](../../docs/proto.md)).

## Entities

`decode_entity` builds an `Entity` from a row:

- `raw`: the row as applied. Mutations parse it, change fields and serialize it again, so fields a
  newer release added survive a Rust write.
- `hot`: the fields readers need on hot paths, decoded once per apply: namespace name, type and
  state; table name, namespace, type, state, schema and partition-list versions, parent table and
  hidden state; tablet tables, state, partition, split depth and children, hidden state; tserver
  registration seqno and state; cluster config version. Everything else is parsed on demand with
  `Entity::parse`.
- `version`: assigned by the materializer on every apply and every load. A mutation records the
  versions it read; any change, including a reload with identical bytes, makes it conflict.

Ids are shared `Arc<[u8]>`, so maps and indexes refer to one copy. Interning them to dense integer
handles is left to the scale work, where memory per tablet is measured.

## YEDIS (D16)

`decode_entity` refuses `REDIS_CONFIG` rows and tables of `REDIS_TABLE_TYPE`. The materializer
turns that into a fatal error naming the row, whether the row comes from a load or from the apply
stream (an older C++ leader could still write one during an upgrade).
