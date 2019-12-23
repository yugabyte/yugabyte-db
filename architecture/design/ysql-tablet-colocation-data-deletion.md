# YSQL Tablet Colocation Drop Flow

## Intro

After some partial work on the drop flow of YSQL tablet colocation, `TRUNCATE`
and `DROP TABLE` do not work for colocated tables.

* `TRUNCATE` removes too much data: all tables in the colocated tablet are
  truncated
* `DROP TABLE` removes too little data: no data for the table is removed

The following details some info to help with a design addressing both concerns.

## Example

Run this on a non-colocated database and a colocated database.

```sql
CREATE TABLE t (i int, j int, k int, PRIMARY KEY (i ASC));
INSERT INTO t VALUES (1, 2, 3), (4, 5, 6), (7, 8, 9);
DELETE FROM t WHERE i = 1;
TRUNCATE TABLE t;
DROP TABLE t;
```

* `CREATE TABLE`:

  * Non-colocated database:

    Create tablets for table

  * Colocated database:

    Do nothing (except metadata changes)

* `INSERT`:

  * Non-colocated database:

    ```
    W1218 18:00:58.233152 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey([], [1]), [SystemColumnId(0)])
    VALUE: null
    W1218 18:00:58.233217 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey([], [1]), [ColumnId(11)])
    VALUE: 2
    W1218 18:00:58.233247 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey([], [1]), [ColumnId(12)])
    VALUE: 3
    W1218 18:00:58.233265 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey([], [4]), [SystemColumnId(0)])
    VALUE: null
    W1218 18:00:58.233280 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey([], [4]), [ColumnId(11)])
    VALUE: 5
    W1218 18:00:58.233295 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey([], [4]), [ColumnId(12)])
    VALUE: 6
    W1218 18:00:58.233311 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey([], [7]), [SystemColumnId(0)])
    VALUE: null
    W1218 18:00:58.233323 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey([], [7]), [ColumnId(11)])
    VALUE: 8
    W1218 18:00:58.233337 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey([], [7]), [ColumnId(12)])
    VALUE: 9
    ```

  * Colocated database:

    ```
    W1218 18:06:30.099133 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], [1]), [SystemColumnId(0)])
    VALUE: null
    W1218 18:06:30.099189 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], [1]), [ColumnId(11)])
    VALUE: 2
    W1218 18:06:30.099215 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], [1]), [ColumnId(12)])
    VALUE: 3
    W1218 18:06:30.099239 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], [4]), [SystemColumnId(0)])
    VALUE: null
    W1218 18:06:30.099251 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], [4]), [ColumnId(11)])
    VALUE: 5
    W1218 18:06:30.099262 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], [4]), [ColumnId(12)])
    VALUE: 6
    W1218 18:06:30.099277 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], [7]), [SystemColumnId(0)])
    VALUE: null
    W1218 18:06:30.099287 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], [7]), [ColumnId(11)])
    VALUE: 8
    W1218 18:06:30.099297 15532 docdb.cc:1343] IntentToWriteRequest
    KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], [7]), [ColumnId(12)])
    VALUE: 9
    ```

* `DELETE`:

  * Non-colocated database:

    ```
    W1218 18:03:01.193349 15532 docdb.cc:345] PrepareNonTransactionWriteBatch
    KEY  : SubDocKey(DocKey([], [1]), [])
    VALUE: DEL
    ```

  * Colocated database:

    ```
    W1218 18:08:15.304158 15532 docdb.cc:345] PrepareNonTransactionWriteBatch
    KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], [1]), [])
    VALUE: DEL
    ```

* `TRUNCATE`:

  * Non-colocated database:

    Destroy and recreate RocksDB

  * Colocated database:

    Destroy and recreate RocksDB

* `DROP TABLE`:

  * Non-colocated database:

    Delete tablets for table

  * Colocated database:

    Do nothing (except metadata changes)

## Design proposals

### Table-level tombstone

`DELETE`ing a row from a table creates a tombstone of the form

```
W1218 18:08:15.304158 15532 docdb.cc:345] PrepareNonTransactionWriteBatch
KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], [1]), [])
VALUE: DEL
```

That invalidates all keys corresponding to that row:

```
W1218 18:06:30.099133 15532 docdb.cc:1343] IntentToWriteRequest
KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], [1]), [SystemColumnId(0)])
VALUE: null
W1218 18:06:30.099189 15532 docdb.cc:1343] IntentToWriteRequest
KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], [1]), [ColumnId(11)])
VALUE: 2
W1218 18:06:30.099215 15532 docdb.cc:1343] IntentToWriteRequest
KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], [1]), [ColumnId(12)])
VALUE: 3
```

It seems to be due to the fact that they all share the same `DocKey`.

Consider extending the idea from the row level to the table level:

```
W1218 xx:xx:xx.xxxxxx 15532 docdb.cc:345] PrepareNonTransactionWriteBatch
KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, [], []), [])
VALUE: DEL
```

I imagine this covers all keys corresponding to that table because they share
a common prefix.  However, they don't share the same `DocKey`: they share a
prefix of the `DocKey`.  This does not seem to be supported and may involve
grafting in `DeleteRange` from RocksDB.

Besides that, this idea is clean and could work well with intents and
transactions.

### Incarnation number

The idea with this is to put extra information in the `DocKey` like so:

```
W1218 xx:xx:xx.xxxxxx 15532 docdb.cc:1343] IntentToWriteRequest
KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, IncarnationNumber=0, [], [1]), [SystemColumnId(0)])
VALUE: null
W1218 xx:xx:xx.xxxxxx 15532 docdb.cc:1343] IntentToWriteRequest
KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, IncarnationNumber=0, [], [1]), [ColumnId(11)])
VALUE: 2
W1218 xx:xx:xx.xxxxxx 15532 docdb.cc:1343] IntentToWriteRequest
KEY  : SubDocKey(DocKey(CoTableId=07400000-0000-0080-0030-000001400000, IncarnationNumber=0, [], [1]), [ColumnId(12)])
VALUE: 3
```

Upon a `TRUNCATE`, the incarnation number can be bumped.  Any `DocKey`s with
unequal incarnation number shall be ignored.  The current incarnation number
for a table should be kept track of in the tablet metadata.  Compactions
should be tweaked to identify and remove the `DocKey`s with invalid incarnation
number.

How much space should the incarnation number take?

* **One byte**: This should be sufficient as it allows for 256 possible
  incarnation numbers.

How should the next incarnation number be selected?

* **Increment:** This makes it difficult to resurface old values that haven't
  been compacted because the incarnation number must cycle 256 times in a short
  period of time for that to happen.  It also simplifies the logic for multiple
  `TRUNCATE`s within a single transaction (see below).

The main challenge with the incarnation number design is handling multiple
sessions/transactions.

How do we handle `ROLLBACK` on a transaction with `TRUNCATE`?

* **Keep track of stable and pending incarnation numbers:** The stable
  incarnation represents the table outside of transactions, and the pending
  incarnation represents the table inside of a transaction.  Any documents with
  an incarnation number between the stable and pending ones (inclusive) are not
  to be compacted.  Therefore, if more than one `TRUNCATE` occurs in a
  transaction, data of old, intermediate tables in the transaction will be
  retained (this may be needed for deferred triggers).  Upon rollback, the
  pending incarnation number may simply be erased such that the effective
  incarnation becomes the stable one, and the intents containing pending and
  intermediate incarnation numbers should be automatically dropped.

How do we handle multiple sessions/transactions with any number of them
performing `TRUNCATE`?

* **Keep track of stable and pending incarnation numbers:** Given that only one
  transaction at a time may do a `TRUNCATE`, it is sufficient to have a single
  pending incarnation number.  This is because `TRUNCATE` should take an
  `ACCESS EXCLUSIVE` lock.  If the locking logic is hooked up properly (it is
  currently not), there is no need to worry about multiple pending `TRUNCATE`s
  or outside accesses to the truncated table.

Another possible problem is that requests for `DROP TABLE` and `TRUNCATE` would
not only have to hit master (for metadata changes through `CatalogManager`) but
also have to hit tserver (for transactional conflict checking through
`TabletServiceImpl::Read` or `TabletServiceImpl::Write`).  This may best be
solved through transactional DDL support which is not yet ready.

### Special document

To mark a table as truncated, we could write a special document to DocDB
indicating the truncate.

In the case of a transactional truncate, the special document could be written
to the intents DB.  Conflict detection would then need to make sure that this
special document conflicts with any other documents that are of the same table
except for those that are of the same transaction.  This means that the
transaction ID should probably be encoded into the special document.  Conflict
detection may be difficult if there is no document iterator that works on
prefixes of `DocKey`s.

When a transactional truncate commits, the special document should not be
written to the regular DB.  Furthermore, the data before truncate must be
discarded: this can be done by hybrid time or perhaps a better method that
requires more work before this step.  The discarding will likely be individual
key deletes, so it would be as expensive as an unconditional `DELETE`.
