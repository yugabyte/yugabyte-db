# Supporting SAVEPOINTS in YSQL

Tracking GitHub Issue: <https://github.com/yugabyte/yugabyte-db/issues/9219>

Savepoints allow a user to create "checkpoints" within a transaction to which they can rollback the state of the transaction without rolling back the entire transaction. Postgres implements savepoints using "subtransactions". In Yugabyte, we leverage Postgres' subtransaction logic with additional changes to our transaction and persistence layers to support savepoints in YSQL.

## Subtransactions

Internally, Postgres represents a transaction as a series of subtransactions, and savepoints can be thought of as the boundaries between subtransactions within a transaction. A transaction is initialized with a subtransaction with id=1. When we create a new savepoint, the transaction moves into a new subtransaction whose id is the lowest unused subtransaction id. For example, if we start a transaction, when we create our first savepoint, we will move to a new subtransaction with id=2. When we rollback to a savepoint, we will again initialize a new subtransaction with the lowest unused subtransaction id. We *never* reuse subtransaction IDs within a transaction. We'll discuss how we associate subtransaction IDs with uncommitted writes, and it's important that we can assume any writes with a given subtransaction ID unambiguously belong to a single specific subtransaction (corresponding to a single specific user-defined savepoint).

### 1. Managing subtransaction state

Postgres already has some accounting for subtransaction and savepoint name state. The Postgres process will maintain a mapping between active savepoint names and subtransaction IDs. Whenever the active subtransaction is changed, we update pggate state accordingly. Additionally, when subtransactions are aborted due to a `ROLLBACK` command, we update pggate state which is tracking a set of aborted subtransaction IDs. `RELEASE` commands only update Postgres' internal state mapping savepoint names to subtransaction IDs and do not affect anything in pggate.

On all read and write requests to tservers, we forward both the active subtransaction ID and the set of aborted subtransaction IDs to the tserver. Written intents are associated with the active subtransaction ID they were written in, and reads will mask any intents which were written during a subtransaction which is now aborted.

### 2. Persistence-layer format

Every provisional record (intent) will now include an optional subtransaction id. The default case (no subtransaction id) would mean subtransaction 1 for backward compatibility.

Since all reads will include an aborted subtransaction ID set, we assume that any subtransaction IDs that are not in this set are live and should be returned to postgres.

### 3. Committing a transaction

When we send an UpdateTransaction RPC to the transaction coordinator to mark it committed, we include a set of aborted subtransaction IDs. We will serialize this set as part of the raft operation which marks this transaction committed in the transaction status table.

When reading intents, we may send an RPC to the transaction coordinator to check the intent's transaction status and determine if it's been comitted or not. These RPCs will now also include the aborted subtransaction set, so we may determine if the intent is actually aborted. In order to allow aborted intents to be ignored by conflict resolution, the client's heartbeat will also include the latest aborted subtransaction set to update in the transaction status table.

### 4. Applying a transaction

Applying a transaction is a Raft-level operation which, once Raft-committed, will be Raft-applied by writing the transaction's intents to the regular db. This Raft-level operation must now include the aborted subtransaction set, so that when it's Raft-applied, aborted intents are not written to the regular DB. Once a transaction APPLYING op is Raft-committed, a node can restart and be able to properly apply the Raft operation using the aborted subtransaction set persisted to the WAL in this operation's message.

When we Raft-commit this APPLYING operation, we also update in-memory data about the commit time of this transaction. Conflict resolution code uses this in-memory state to avoid making an RPC to the transaction status tablet to determine if intents should be treated as committed. This state also needs to be augmented to include the aborted subtransaction set, so aborted intents can be ignored.

For sufficiently large transactions (see `FLAGS_txn_max_apply_batch_records`), we apply intent records in batches, along with an `ApplyTransactionState` record. In case a node crashes and restarts after a batch of intents starts applying but before we have applied all batches, we're able to load the aborted subtransaction state from the persisted `ApplyTransactionState` record. This effects the in-memory state used to track commit status of transactions as well as the state used to continue application of this transaction.

### Notes

Although we best-effort transmit the aborted subtransaction set from the client to the coordinator via status heartbeats, in order to support semantics identical to Postgres, we should synchronously drop explicit locks taken during a now-aborted subtransaction (<https://github.com/yugabyte/yugabyte-db/issues/10039>). This would require transmitting new aborted subtransaction state to the coordinator synchronously when the user issues a `ROLLBACK TO [savepoint]` command.