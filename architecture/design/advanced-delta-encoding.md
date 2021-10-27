Assume we have a table created with `CREATE TABLE t (h int, r int, c1 int, c2 int, PRIMARY KEY (h, r));`
And we've inserted a single row using `INSERT INTO t(h, r, c1, c2) VALUES (1, 2, 12, 24);`

According to https://docs.yugabyte.com/latest/architecture/docdb/persistence this is represented in docdb as the following 3 separate records:
```
SubDocKey(DocKey(0x1210, [1], [2]), [SystemColumnId(0); HT{ physical: 1634209096289349 }]) -> null
SubDocKey(DocKey(0x1210, [1], [2]), [ColumnId(12); HT{ physical: 1634209096289349 w: 1 }]) -> 12
SubDocKey(DocKey(0x1210, [1], [2]), [ColumnId(13); HT{ physical: 1634209096289349 w: 2 }]) -> 24
```

Which then encoded in binary format and stored in underlying rocksdb as:
```
471210488000000121488000000121 4A80 23800185F0027D73BA804A   0114000000000004 -> null
471210488000000121488000000121 4B8C 23800185F0027D73BA803FAB 0115000000000004 -> 12
471210488000000121488000000121 4B8D 23800185F0027D73BA803F8B 0116000000000004 -> 24
```
The last 8 bytes of each binary record representation above is rocksdb internal suffix which contains value type (kTypeValue = 0x01) and sequence number (increments for each new record).

Standard rocksdb key delta encoding saves some space by extracting common shared prefix and encoding those records using the following scheme: `<shared_prefix_size><non_shared_size><value_size><non_shared><value>`. For our example this will be encoded as:
```
471210488000000121488000000121 4A80 23800185F0027D73BA804A   0114000000000004 -> null => <0><24><0><0x4712104880000001214880000001214A8023800185F0027D73BA804A0114000000000004><>
471210488000000121488000000121 4B8C 23800185F0027D73BA803FAB 0115000000000004 -> 12 => <15><32><0x4B8C23800185F0027D73BA803FAB0115000000000004><12>
471210488000000121488000000121 4B8D 23800185F0027D73BA803F8B 0116000000000004 -> 24 => <16><31><0x8C23800185F0027D73BA803FAB0115000000000004><24>
```

But the only difference between keys of each of these docdb records and the next one is only `ColumnId` and `w` (write_id) component of hybrid time. In the binary form, we also have the last internal rocksdb component which is usually just got incremented for the next record. So if we can only store this difference, but not the whole key part after shared_prefix - we can save more space.

Difference between 1st and 2nd encoded docdb keys:
```
471210488000000121488000000121 4A80 23800185F0027D73BA80 4A   0114000000000004
471210488000000121488000000121 4B8C 23800185F0027D73BA80 3FAB 0115000000000004
                               ^^^^                      ^^^^   ^^
```
So, we can just store 2+2=4 bytes that are not reused from the previous key (plus additional info about key component sizes and flag that the last internal component is reused and incremented) instead of 32 bytes for the whole key part after the prefix.

Difference between 2nd and 3rd encoded docdb keys:
```
4712104880000001214880000001214B 8C 23800185F0027D73BA803F AB 0115000000000004
4712104880000001214880000001214B 8D 23800185F0027D73BA803F 8B 0116000000000004
                                 ^^                        ^^   ^^
```
Here we can store 1+1=2 bytes that are not reused from the previous key (plus additional info as above) instead of 31 bytes for the whole key part after the prefix.

More columns per row we have - more space we can save using this approach.

In order to implement this at rocksdb level we try to match previous and current encoded keys to the following keys pattern:

Prev key: `<shared_prefix>[<prev_key_non_shared_1>[<shared_middle>[<prev_key_non_shared_2>]]][<last_internal_component_to_reuse>]`

Current key: `<shared_prefix>[<non_shared_1>[<shared_middle>[<non_shared_2>]]][<last_internal_component_to_reuse> (optionally incremented)]`

And then we encode information about these component sizes and whether `last_internal_component` is reused and if it is incremented or reused as is (this can happen in snapshot SST files where the sequence number is reset to zero). 
After this information we just need to store `<non_shared_1><non_shared_2><value>` instead of `<non_shared><value>` (where `<non_shared>` is everything after `<shared_prefix>`).
