# Motivation

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
471210488000000121488000000121 4B8C 23800185F0027D73BA803FAB 0115000000000004 -> 12 => <15><32><4><0x4B8C23800185F0027D73BA803FAB0115000000000004><12>
471210488000000121488000000121 4B8D 23800185F0027D73BA803F8B 0116000000000004 -> 24 => <16><31><4><0x8D23800185F0027D73BA803FAB0115000000000004><24>
```

But the only difference between keys of each of these docdb records and the next one is only `ColumnId` and `w` (write_id) component of hybrid time. In the binary form, we also have the last internal rocksdb component which is usually just gets incremented for the next record. So if we can only store this difference, but not the whole key part after shared_prefix - we can save more space.

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

# Approach

In order to implement this at rocksdb level we try to match previous and current encoded keys to the following keys pattern and maximize `<shared_prefix>` and `<shared_middle>` size:

Previous key: `<shared_prefix>[<prev_key_non_shared_1>[<shared_middle>[<prev_key_non_shared_2>]]][<last_internal_component_to_reuse>]`

Current key: `<shared_prefix>[<non_shared_1>[<shared_middle>[<non_shared_2>]]][<last_internal_component_to_reuse> (optionally incremented)]`

`<last_internal_component_to_reuse>` always has size of 8 bytes (if internal rocksdb component is the same in previous and current keys or just its embedded sequence number is incremented) or 0 bytes (in other cases).

Then we encode information about these component sizes and whether `last_internal_component` is reused and if it is incremented or reused as is (this can happen in snapshot SST files where the sequence number is reset to zero). 
After this information we just need to store `<non_shared_1><non_shared_2><value>` instead of `<non_shared><value>` (where `<non_shared>` is everything after `<shared_prefix>`).

# Encoding format details

As a results of the approach described above we have following components sizes/flag that fully determine difference between previous key and current key:
- `shared_prefix_size`
- `prev_key_non_shared_1_size`
- `non_shared_1_size`
- `shared_middle_size`
- `prev_key_non_shared_2_size`
- `non_shared_2_size`
- `last_internal_component_reuse_size` (0 or 8)
- `is_last_internal_component_inc` (whether last internal component reused from previous key is incremented)

Note that previous key size is always `shared_prefix_size + prev_key_non_shared_1_size + shared_middle_size + prev_key_non_shared_2_size + last_internal_component_reuse_size`, so we can compute `shared_middle_size` based on `shared_prefix_size`, `prev_key_non_shared_1_size`, `prev_key_non_shared_2_size`, `last_internal_component_reuse_size` and previous key size.

And current key size is always `shared_prefix_size + non_shared_1_size + shared_middle_size + non_shared_2_size + last_internal_component_reuse_size`. 

We will store `non_shared_i_size_delta = non_shared_i_size - prev_key_non_shared_i_size` (i = 1, 2) for efficiency, because for DocDB these deltas are `0` in most cases and we can encode this more efficiently.

So, to be able to decode key-value pair it is enough to know whole previous key and store the following:
- Sizes & flags:
  - `shared_prefix_size`
  - `non_shared_1_size`
  - `non_shared_1_size_delta`
  - `non_shared_2_size`
  - `non_shared_2_size_delta`
  - `bool is_last_internal_component_reused` 
  - `bool is_last_internal_component_inc`
  - `value_size`
- Non shared bytes:
  - `non_shared_1` bytes
  - `non_shared_2` bytes
  - `value` bytes

The general encoding format is therefore: `<sizes & flags encoded><non_shared_1 bytes>[<non_shared_2 bytes>]<value bytes>`.

## Sizes & flags encoding

In general, sizes & flags encoding format is:
`<encoded_1><...>`, where `encoded_1` is an encoded varint64 containing the following information:
- bit 0: is it most frequent case (see below)?
- bit 1: `is_last_internal_component_inc`
- bits 2-...: `value_size`

We encode these two flags together with the `value_size`, because if `value_size` is less than 32 bytes, then `encoded_1` will still be encoded as 1 byte. 
Otherwise one more byte is negligible comparing to size of the value that is not delta compressed.

We have several cases that occur most frequently and we encode them in a special way for space-efficiency, depending on the case `<...>` is encoded differently:

1. Most frequent case:
- `is_last_internal_component_reused == true`
- `non_shared_1_size == 1`
- `non_shared_2_size == 1`
- `non_shared_1_size_delta == 0`
- `non_shared_2_size_delta == 0`

In this case we know all sizes and flags except `shared_prefix_size`, so we just need to store: `<encoded_1><shared_prefix_size>`

2. The format is: `<encoded_1><encoded_2><...>`, where `encoded_2` is one byte that determines which subcase we are dealing with. In some cases `encoded_2` also contains some more useful information.

2.1. Something is reused from the previous key (meaning `shared_prefix_size + shared_middle_size + last_internal_component_reuse_size > 0`):

2.1.1. Optimized for the following case:
- `is_last_internal_component_reused == true`
- `non_shared_1_size_delta == 0`
- `non_shared_2_size_delta == 0`
- `non_shared_1_size < 8`
- `non_shared_2_size < 4`

In this case `encoded_2` is:
- bit 0: `1`
- bit 1: `0`
- bit 2: `non_shared_2_size_delta == 1`
- bits 3-5: `non_shared_1_size`
- bits 6-7: `non_shared_2_size`

We store: `<encoded_1><encoded_2><shared_prefix_size>` and the rest of sizes and flags is computed based on this.

2.1.2: Rest of the cases when we reuse bytes from the previous key.

In this case `encoded_2` is:
- bit 0: `1`
- bit 1: `1`
- bit 2: `is_last_internal_component_reused`
- bit 3: non_shared_1_size_delta != 0
- bit 4: non_shared_2_size != 0
- bit 5: non_shared_2_size_delta != 0
- bits 6-7: not used

We store: `<encoded_1><encoded_2><non_shared_1_size>[<non_shared_1_size_delta>][<non_shared_2_size>][<non_shared_2_size_delta>]<shared_prefix_size>`, sizes that we know based on `encoded_2` bits are zeros are not stored to save space.

2.2. Nothing is reused from the previous key (mostly restart keys).
In this case we just need to store key size and value size.

2.2.1. `0 < key_size < 128`.

In this case `encoded_2` is:
- bit 0: `0`
- bits 1-7: `key_size` (>0)

And we just need to store: `<encoded_1><encoded_2>` to be able to decode key size and value size.

2.2.2. `key_size == 0 || key_size >= 128`

In this case `encoded_2` is just `0`:
- bit 0: `0`
- bits 1-7: `0`

We store: `<encoded_1><encoded_2=0><key_size>` that is enough to decode key size and value size.
