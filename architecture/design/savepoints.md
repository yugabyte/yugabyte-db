# Supporting SAVEPOINTS in YSQL

The tracking GitHub issue for this feature is [[9219](https://github.com/yugabyte/yugabyte-db/issues/9219)].

Savepoints allow you to create "checkpoints" within a transaction. You can use these checkpoints to roll back the _state_ of the transaction without rolling back the _entire_ transaction. PostgreSQL implements savepoints using "subtransactions". In YugabyteDB, we leverage PostgreSQL's subtransaction logic with additional changes to our transaction and persistence layers to support savepoints in YSQL.

## Subtransactions

Internally, PostgreSQL represents a transaction as a series of subtransactions, and you can think of savepoints as the _boundaries between subtransactions_ within a transaction.

We initialize new transactions with a subtransaction of ID=1. When you create a new savepoint, the transaction moves into a new subtransaction whose ID is the lowest unused subtransaction ID. For example, if you start a transaction and create your first savepoint, we move to a new subtransaction with ID=2. We _never_ reuse subtransaction IDs within a transaction. When you roll back to a savepoint, we again initialize a new subtransaction with the lowest unused subtransaction ID, which in this case is 3.

We'll discuss how we associate subtransaction IDs with uncommitted writes, and it's important that we can assume that any writes with a given subtransaction ID unambiguously belong to a single specific subtransaction (corresponding to a single specific user-defined savepoint).

### 1. Managing subtransaction state

PostgreSQL performs some accounting for subtransaction and savepoint name states. The PostgreSQL process maintains a mapping between active savepoint names and subtransaction IDs. Whenever the active subtransaction is changed, we update the `pggate` state accordingly. Additionally, when subtransactions are canceled with a `ROLLBACK` command, we update the `pggate` state which is tracking a set of canceled subtransaction IDs. `RELEASE` commands only update PostgreSQL's internal state mapping savepoint names to subtransaction IDs, and do not affect anything in `pggate`.

On all read and write requests to tablet servers, we forward both the active subtransaction ID and the set of aborted subtransaction IDs to the tablet server. Written intents are associated with the active subtransaction ID they were written in, and reads will mask any intents which were written during a subtransaction which is now aborted.

### 2. Persistence-layer format

Every provisional record (intent) now includes an optional subtransaction ID. The default case (no subtransaction ID) implies a subtransaction ID of 1 for backward compatibility.

Since all reads include a set of canceled-subtransaction IDs, we assume that any subtransaction IDs not in this set are live and should be returned to PostgreSQL.

### 3. Committing a transaction

When we send an `UpdateTransaction` RPC to the transaction coordinator to mark a transaction as committed, we include a set of canceled subtransaction IDs. We serialize this set as part of the Raft operation which marks this transaction committed in the transaction status table.

When reading intents, we may send an RPC to the transaction coordinator to check the intent's transaction status and determine whether it's been committed. These RPCs also include the canceled subtransaction set, so we may determine if the intent is actually canceled. In order to allow canceled intents to be ignored by conflict resolution, the client's heartbeat will also include the latest canceled subtransaction set to update in the transaction status table.

### 4. Applying a transaction

Applying a transaction is a Raft-level operation which, once Raft-committed, will be Raft-applied by writing the transaction's intents to the regular database. This Raft-level operation must now include the canceled subtransaction set, so that when it's Raft-applied, canceled intents are not written to the regular database. Once a transaction `APPLYING` operation is Raft-committed, a node can restart and be able to properly apply the Raft operation using the canceled subtransaction set persisted to the WAL in this operation's message.

When we Raft-commit this `APPLYING` operation, we also update in-memory data about the operation's commit time. Conflict resolution code uses this in-memory state to avoid making an RPC to the transaction status tablet to determine if intents should be treated as committed. This state is also augmented to include the canceled subtransaction set, so canceled intents can be ignored.

For sufficiently large transactions (see `FLAGS_txn_max_apply_batch_records`), we apply intent records in batches, along with an `ApplyTransactionState` record. If a node crashes and restarts after a batch of intents starts applying, but before we have applied all batches, we're able to load the canceled subtransaction state from the persisted `ApplyTransactionState` record. This affects the in-memory state used to track commit status of transactions as well as the state used to continue application of the current transaction.

### Notes

Although we make our best effort to transmit the canceled subtransaction set from the client to the coordinator via status heartbeats, in order to support semantics identical to PostgreSQL, we would need to synchronously drop explicit locks taken during a now-canceled subtransaction (<https://github.com/yugabyte/yugabyte-db/issues/10039>). This would require transmitting the new canceled subtransaction state to the coordinator synchronously when the user issues a `ROLLBACK TO [savepoint]` command.
