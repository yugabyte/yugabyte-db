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

* One byte should be sufficient as it allows for 256 possible incarnation
  numbers.

How should the next incarnation number be selected?

* Increment: This could get confusing with multiple sessions/transactions, but
  as long as conflicts are detected, I imagine that we should be able to avoid
  weird states.
* Random number: Given the sample space of 256 numbers, it is dangerously easy
  to choose an old incarnation number and possibly resurface old values that
  haven't been compacted.  However, if we could guarantee that unique numbers
  are chosen, this could make multiple session/transaction handling easier.  So
  far, it seems like multiple concurrent `TRUNCATE`s is not possible.

The main challenge with the incarnation number design is handling multiple
sessions/transactions.

* How do we handle `ROLLBACK` on a transaction with `TRUNCATE`?  If we update
  the incarnation number for the table in the tablet metadata, compactions may
  remove the data of the table before `ROLLBACK`.  We could create an
  intermediate state such that compactions know a transaction is in progress.
  So there would ever be at most two incarnation numbers: the current
  incarnation number and an optional intermediate incarnation number.
* How do we handle multiple sessions/transactions with any number of them
  performing `TRUNCATE`?  The tablet metadata is global to the raft group for
  the colocated tablet.  To isolate different sessions, we can indicate the
  corresponding transaction ID alongside the incarnation number in the
  metadata.  The problem is that each request would need to send
  transaction ID information to use the correct incarnation number.  Regarding
  conflict detection, if a request sees that there's an intermediate
  incarnation with different transaction id on the same table, a conflict
  should be thrown.  This is because `TRUNCATE` takes an `ACCESS EXCLUSIVE`
  lock.  This enforces that only one `TRUNCATE` should ever be pending at a
  time.
