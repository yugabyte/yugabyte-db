# Review conventions for managed/ (YugabyteDB Anywhere)

These apply to changes under `managed/` in addition to the repo-root [REVIEW.md](../REVIEW.md).
Skip this file for backport PRs. For v2 API changes, also check the contract rules in
[AGENTS.md](AGENTS.md#contract-rules).

YBA orchestrates long-lived universes and upgrades in place, keeping its own database. Most YBA
bugs that reach customers are not wrong lines in the diff. They come from a change that holds on
one path and breaks on a sibling path, on state an older YBA left behind, or on a provider or
deployment the author didn't test. The checks below cover those cases. Report at **HIGH** a finding
that can break a universe operation, lose or leak data or secrets, or leave a universe locked;
report the rest at **MEDIUM**.

## Sibling paths

A fix or new check on one path must hold on its siblings, or the PR must say why a sibling is out
of scope. Check each that applies:

- **API versions:** v1 controllers and request binders (`controllers/`,
  `UniverseControllerRequestBinder`) and v2 handlers and mappers (`api/v2/handlers/`,
  `api/v2/mappers/`). A validation added to one is often missing from the other.
- **Clusters:** the primary cluster and read replica (ASYNC) clusters. Task params may carry only
  one cluster, and `getClusterByUuid` returns null for a cluster the request left out.
- **Server types:** master and tserver. Code that builds per-server config (gflags, Helm overrides,
  HBA conf, certificates) must pass the same inputs to both.
- **Deployments:** VM (cloud and on-prem) and Kubernetes (Helm and operator-owned universes); for
  YBA itself, YBA Installer and Helm. Operator-owned universes are changed by
  `YBUniverseReconciler`, which builds its own overrides, and the API rejects many direct edits to
  them.
- **Providers:** AWS, GCP, Azure, OCI, on-prem and Kubernetes differ in destroy idempotency (GCP
  static IPs), resource ownership (Azure NICs live in the network subscription), identity (IAM
  roles, federation, managed identity) and images. A claim tested on one provider needs evidence
  for the others the change touches.
- **Clients:** the UI (the legacy and the `redesign/` flows), `yba-cli`, the Terraform provider and
  the operator call the same APIs. A changed default or a newly required field changes behavior for
  all of them.
- **Paths that rebuild universe state:** create, edit, resize, instance-type or vCPU change, gflags
  upgrade, in-memory gflags update (`SetFlagInMemory`), software upgrade, rollback and retry.

## State an older YBA left behind

- **Persisted task params.** A failed task saved by an older build can be retried or rolled back on
  the new one. A new field is absent from those params, not false, and values redacted when the
  task was saved stay redacted.
- **Universes already broken.** A fix for a state that freezes a universe must also unblock
  universes that are already in it, or the PR must say how support recovers them.
- **Existing objects.** New validation on stored providers, storage configs, universes or releases
  blocks unrelated edits (a rename, a credential rotation) on objects created before the PR. Gate
  it behind a runtime config key, or apply it only to new objects.
- **Persisted enums.** Enum values are stored in table columns (Ebean `@Enumerated`, `@EnumValue`,
  `@DbEnumValue`) and inside JSON (universe details, task params, provider details). Removing or
  renaming a value breaks loading every row that still holds it: Ebean fails the row, and Jackson
  throws on the unknown value, so the universe or task list can't be loaded and the task can't be
  retried. Keep the value and mark it `@Deprecated` (`TaskType.filteredValues()` hides those), or
  add a migration that rewrites the stored values first. Renaming the Java constant while keeping
  the `@EnumValue` string still breaks the JSON copies, which Jackson reads by `name()`. Where
  unknown values can arrive, for example from a newer YBA, read with `@JsonEnumDefaultValue` and
  `READ_UNKNOWN_ENUM_VALUES_USING_DEFAULT_VALUE`, as `YbcBackupUtil` does.
- **Migrations.** Platform database migrations and `yba-ctl` state migrations must be correct for
  every version they can upgrade from, including installs that already took a later path.
- **Node agent.** Node agents upgrade after YBA, so a new YBA talks to old node agents. Changes to
  `node-agent/proto/` follow the root guide's protobuf rules, and a new RPC or field needs a
  fallback for an old agent.

## Saved JSON and POJO changes

Much of YBA's state is a POJO stored as JSON: universe details (`universe_details_json`), task
params, provider and region details, customer configs. Every stored copy was written by some older
build, and v1 APIs take the same POJOs as request bodies, so a POJO change is a storage change and
an API change at once. The shared mapper (`CustomObjectMapperModule`) ignores unknown properties
and leaves out nulls, which decides how each kind of change fails:

- **Removed or renamed field:** no error. Stored values are dropped when read and lost at the next
  save. A rename needs `@JsonAlias` for the old name, or a migration that rewrites the stored JSON.
- **Changed type** (a string to a list, a scalar to an object, a different enum): reading every old
  copy throws, so the universe or task can't be loaded. Add a new field instead, and read the old
  one as a fallback.
- **Moved field** (for example into a nested or per-provider object): code that reads only the new
  location sees nothing on old data. Read both until a migration has moved the stored values.
- **New field:** absent in old JSON. A primitive comes back as `false` or `0`, and a boxed type as
  `null`; neither proves the user chose that value. Pick a default that keeps old behavior, and
  don't let "absent" turn on a new behavior.
- **New getter:** Jackson writes a getter that looks like a property into the stored JSON. Mark
  derived getters `@JsonIgnore`, or the stored copy keeps a stale value that a later field of the
  same name will read.
- **Constructors and polymorphism:** a class without a no-arg constructor, or one built through
  `@JsonCreator`, must accept old JSON that lacks the new arguments. A removed or renamed
  `@JsonSubTypes` name breaks every stored object of that type.
- **Mapper:** read stored JSON with `Json.mapper()` or the injected mapper. A new `ObjectMapper()`
  fails on unknown properties by default, so it throws on old data the shared mapper accepts.

Test a POJO change by reading JSON an older build wrote: a fixture in the test or a migration test,
not a round trip through the new class.

## Older DB, YBC and chart versions

YBA manages universes on DB releases that are years old. A new gflag, RPC, `yb-client` call,
AutoFlags operation or Helm override must be gated on the universe's DB version
(`Util.compareYbVersions`), or handle the older server's answer: an older master can return
`NotSupported` or an unknown-method error. The same applies to YBC versions. Helm charts ship with
each DB release, so an override key an older chart doesn't read is ignored without an error.

## Universe and task safety

- **Locks and in-progress markers.** Every exit path, including abort, exceptions and a failed
  precheck, releases the universe lock and clears `updateInProgress` and
  `placementModificationTaskUuid` as the task framework expects. Otherwise the universe stays
  frozen until support edits it by hand.
- **Concurrency.** A task holds its universe's lock, but scheduled jobs (health checks, backup
  garbage collection, alerts) and API reads run alongside it. Change universe details through
  `Universe.saveDetails` with an updater; reading the universe, changing it and calling `save()`
  loses concurrent updates.
- **Task framework.** A new task is added to `TaskType` (and `CustomerTask.TaskType` if users see
  it). Mark it `@Retryable` or `@Abortable` only if a retry or abort is safe after any of its
  subtasks. Build each subtask group once, not once per node inside a node loop.
- **Gflags survive.** User gflags, per-AZ overrides and gflags YBA adds itself (for example the
  `yb_enable_cbo` entry in `ysql_pg_conf_csv`) survive every path that rebuilds universe state.
  A lost gflag stays silent until the next restart.
- **Defaults that are on.** A runtime config key or feature flag that defaults to true puts the new
  path in front of every customer, whether or not they use the feature.
- **Prechecks.** A new precheck is scoped to the universe being changed. Failing an upgrade or edit
  over an orphaned provider node or an unrelated universe blocks customers with no fix on their
  side.
- **Commands on DB nodes.** Commands YBA runs on database nodes, including health checks, must be
  cheap: reading `/proc/<pid>/smaps` of a large process, for example, stalls it.
- **Deletion.** Every deletion of data or metadata is logged, and force paths skip only the errors
  they name.

## Non-atomic changes and retries

A task changes things YBA does not control in one transaction: cloud resources, Kubernetes objects,
files and processes on nodes, master and tserver state, and object storage. Each of those changes
and the YBA database row that records it are separate steps, and YBA can stop between them: a
subtask fails, the task is aborted, or YBA restarts, which marks every running task failed
(`PLATFORM_RESTARTED` in `CustomerTaskManager`). The user then retries the task with its stored
params, and every subtask runs again from the start. Review each change for the gap between doing
it and recording it:

- **Done outside, not recorded.** A VM, disk, load balancer, IP or bucket object was created but
  YBA didn't save its ID or state. The retry must find it (by a name or tag derived from stored
  values, as the `instanceExists` checks do) and reuse it, not create a second one or leak the first.
- **Recorded, not done.** YBA saved the new state (node state, universe details, a config row)
  before the outside change, and the change failed. The retry must not skip the change because the
  record says it is done; check the real state where it is cheap.
- **Order.** Save what you are about to do before doing it (so a retry or a cleanup knows to look
  for it), and save the result after. Never hold a database transaction open across a cloud, RPC or
  node call: the call can succeed while the transaction rolls back.
- **Retry-safe operations.** Create, add and delete steps succeed when the target is already in the
  end state: an existing resource, a server already in the master config, a resource already gone
  (treat only "not found" as success, not every error). Use the idempotent form where one exists,
  such as `helm upgrade --install`.
- **Stable identity.** A name, UUID or token generated during the task is regenerated on retry
  unless it was saved first, which creates a second resource next to the first.
- **Planning on retry.** A retried task plans its subtasks again from universe state the first
  attempt already changed. A subtask guarded by a condition that attempt flipped (`if isMaster`, a
  node state) is then skipped although it never ran. Plan from the stored params or the original
  state, not from state the task itself changes.
- **Abort.** An aborted subtask can leave its outside operation running. The next retry or task must
  tolerate an operation still in flight or finishing late.
- **Rollback and cleanup** have the same gaps in reverse: deleting a resource that was never
  created, or that a previous cleanup already deleted, must succeed.

Test it with `CommissionerBaseTest.verifyTaskRetries`, which aborts the task before each subtask in
turn and retries it, failing if a retry skips a pending subtask. It runs against mocks, so for
outside resources also check that the subtask's create and delete calls are safe to repeat.

## Thread safety

YBA runs API requests on Play threads, tasks and subtasks on executor threads, and scheduled jobs
on their own threads, all in one JVM. Assume any shared code runs on several threads at once.

- **Shared objects.** Guice `@Singleton` classes, static fields and caches are used by every
  thread. Mutable fields in them need a concurrent type or a lock: a `HashMap` written by two
  threads corrupts silently, and `SimpleDateFormat` isn't thread-safe.
- **Parallel subtasks.** The subtasks of one `SubTaskGroup` run concurrently. They must not write to
  a shared object without synchronization: the task's params, a universe or node object passed to
  each of them, or a shared list or counter.
- **Check-then-act.** A check followed by an action ("if absent, create"; "if not running, start")
  is a race unless one lock or an atomic operation covers both. That includes database rows: two
  requests can both pass a uniqueness check before either inserts, unless a unique constraint
  stops the second one.
- **Locks.** Keep lock scopes short: no cloud, RPC, node or long database call while holding a
  lock, and no blocking work inside `computeIfAbsent`, which blocks every other caller of that map
  until it returns. Code that takes more than one lock takes them in the same order everywhere.
- **Ebean models** are not thread-safe. Don't share one instance between threads; each thread
  loads its own copy, and refreshes before acting on it.
- **Executors.** Create pools with `PlatformExecutorFactory`: its executors carry the log context
  (MDC) to worker threads, take their size from config, and shut down with YBA. A raw
  `Executors.new...` pool loses the log context, and one created per request or with an unbounded
  queue exhausts threads or memory under load.
- **Go (node agent).** Maps and struct fields shared between goroutines need a mutex, goroutines
  must stop when their context is canceled, and tests that touch shared state should pass with
  `-race`.

## API performance

One YBA can manage hundreds of universes and thousands of nodes, and the UI polls the universe,
task and alert endpoints every few seconds. A cost per row or per node in a request handler is
multiplied by both, and a slow handler holds a Play thread that other requests are waiting for.

- **Reads in list calls.** Load a list with one query, or a few batched ones, not one query per
  row. Don't load each row's full universe details or task details when the response doesn't need
  them; use the lighter loaders (`Universe.getAllWithoutResources(uuids)`) or select only the
  columns needed. Filter, sort and page in the query, not in Java over a full list.
- **Outside calls in a request.** A call to a cloud API, a node (SSH or node agent), a master or
  tserver (`yb-client`, HTTP), Prometheus, YBC, LDAP or a webhook can take seconds or hang until
  its timeout. In a request handler, and above all in a list or polled endpoint:
  - don't make one per row or node; if the response really needs them, run them in parallel on a
    bounded pool with a short timeout, and return partial results when some fail;
  - skip targets that can't answer: a node whose API or port is disabled costs a full connect
    timeout on every call (Live Queries probing the YCQL port on universes with YCQL disabled);
  - prefer reading what a background job stored, or a cache, over calling out on every request.
- **Long work** runs as a task: the request submits it and returns the task UUID.
- **Timeouts.** Every outside call in a request path has an explicit timeout, shorter than the
  load balancer's and the client's. One unreachable node must not fail or stall the whole response.
- **Caches** are bounded, thread-safe (see Thread safety) and invalidated when the data changes.
  Decide whether a failure is cached: not caching it repeats a slow failure on every call.
- **Scheduled jobs** that loop over universes or nodes must not let one slow target delay all the
  others. Schedule them with `PlatformScheduler`, which skips a run while the previous one is still
  going.

## Silent failures and nulls

A failure that is logged and dropped surfaces later as a different, harder problem: a task marked
successful that didn't do its work, a secret left unredacted, a universe in a state nobody chose.

- **Swallowed exceptions.** A `catch` that logs and continues, or returns `null`, `false`, an empty
  list or `Optional.empty()`, is a finding when the caller needs to know. The caller can't tell
  "failed" from "nothing there": a redaction helper that catches everything returns the input
  unredacted, and a lookup that returns an empty list on error looks like "no secrets". Catch the
  narrowest exception that is really expected; let the rest propagate.
- **Tasks fail by throwing.** The task framework marks a subtask failed only when it throws. A
  subtask that catches its own error and returns lets the task finish as successful. Errors that
  may be ignored are ignored explicitly, with the `ignoreErrors` option of `SubTaskGroup`.
- **Outside results.** Check the result of every outside call: `ShellResponse.processErrors()` for
  shell and `ybops`, `hasError()` on `yb-client` responses (a response can carry an error without
  an exception), HTTP status codes, and exit codes. Shell scripts use `set -e` (and `pipefail` in
  bash), and `|| true` needs a reason.
- **Async work.** An exception in a `CompletableFuture` or a submitted task that nobody joins is
  lost. Join it, or attach a handler that records the failure where the caller will see it.
- **Keep the cause.** A rethrow wraps the original exception (`new X(msg, e)`), and a user-facing
  error says what failed and what to do. A bare "NullPointerException" in a task error tells the
  user nothing.
- **Go and Python.** Every Go `err` is returned or handled, not assigned to `_` or only logged where
  the caller needs it. Python under `devops/` doesn't use a bare `except:`.

Null pointer exceptions:

- **Where nulls come from.** Methods that return null when something is missing
  (`getClusterByUuid`, node and map lookups), fields absent from older saved JSON (see Saved JSON
  and POJO changes), optional request fields, and unset boxed fields in `UserIntent` and task
  params.
- **Check at the boundary.** Validate request input and loaded state where it enters, and fail with
  a clear `PlatformServiceException` (400 or 404), instead of passing a null deep into a task where
  the NPE fires far from its cause, often after the task has already changed something.
- **Unboxing.** `if (intent.someFlag)` or arithmetic on a boxed `Boolean` or `Integer` throws when
  the field is null. Use `Boolean.TRUE.equals(...)` or a default.
- **Optional and streams.** No `Optional.get()` or `findFirst().get()` without handling the empty
  case; use `orElseThrow` with a message. Prefer `maybeGet` or `getOrBadRequest`-style loaders that
  make the missing case explicit.
- **Making it worse.** A null check that only avoids the NPE, skipping the work without an error,
  turns a crash into a silent no-op. Decide what a missing value means and handle that.

## Memory and resource leaks

YBA and node agent run for months without a restart, so a small leak per request, task or
universe adds up until the JVM runs out of heap or the host runs out of threads, files or disk.

- **Collections that only grow.** A static or singleton map, set or list keyed by universe, task,
  node or request needs a removal path: when the task ends (on failure and abort too), when the
  universe is deleted, or a size and age bound (Caffeine or Guava cache, not a bare `HashMap`).
- **Loading whole tables.** Don't load every row of a large table (`task_info`, `audit`, alerts,
  metrics) into a list. Page through it, or stream it with `findEach`.
- **Unbounded reads.** Don't read a command's output, a file, an HTTP response or a log into memory
  without a size limit: `kubectl`, `ybops` and node commands can return megabytes.
- **Closeable resources.** Close every `YBClientApi` (try-with-resources, as the rest of the code
  does), streams, readers, HTTP responses, process handles and Kubernetes clients, on the error
  path too. An unclosed client keeps its threads and connections.
- **Threads and executors.** An executor created per call and never shut down leaks its threads. So
  does a scheduled job registered again on every config change.
- **Listeners and informers.** Callbacks, watchers and Kubernetes informers registered at runtime
  must be removed or stopped when their universe or config goes away.
- **Metrics.** A metric labeled by universe, node or task adds a series that lives until it is
  removed. Remove the label values when the object is deleted, and don't label by unbounded values
  such as task UUIDs.
- **Temporary files** (support bundles, backup scripts, downloaded releases, rendered templates) are
  deleted on failure as well as on success.
- **Go (node agent).** Close HTTP response bodies, files and gRPC streams, and make sure goroutines
  exit (see Thread safety): a goroutine blocked forever keeps everything it references. Maps keyed
  by task or request need a removal path, as above.

## API, RBAC, audit and runtime config

- A new or changed endpoint declares its RBAC permissions (`@AuthzPath` and the required
  permissions), writes an audit entry when it changes something, and scopes every lookup to the
  customer. For v2, see the contract rules in [AGENTS.md](AGENTS.md#contract-rules).
- A new runtime config key has the right scope (global, customer, universe or provider), a safe
  default, and the `PUBLIC` tag only if users are meant to set it.

## YBA high availability

Only the platform database is replicated to HA standbys (`PlatformReplicationManager`). Files on
the YBA host, such as certificates, keys, uploaded releases and installer config, are not. A feature
that keeps state in local files must work after a failover, or the PR must say it doesn't.

## Health checks and alerts

- `health/node_health.py.template` runs on every DB node on a schedule. It must stay cheap and run
  on every OS and Python version YBA supports.
- A change to a health check or alert must not fire on healthy universes, and must not stop firing
  on the failure it watches. False alerts and missing alerts have both reached customers.

## Secrets

A sensitive value is redacted everywhere it is stored or printed, not only in the code the PR
touches: task params, audit rows (including rows already stored), application and task logs,
shell command output, Helm value output, API responses and support bundles. Redaction exists in
more than one copy, Java `RedactingService` and the Go support-bundle redactor in
`devops/supportbundle/serverconfredact/`, so a fix to one is needed in the other.

## Node agent and YNP

- Steps that run as root must not write through paths the `yugabyte` user owns or can replace.
  `O_NOFOLLOW` checks only the last path component, so a user-owned parent directory can still be
  swapped for a symlink.
- Each step runs as the user it needs: root modules as root only, the rest as the service user.
- systemd: support root-level and user-level units, and nodes that have only one of them.
- The boot disk of a VM can be replaced, so the owner UID of an existing data directory (such as
  `/mnt/d0`) can differ from the new `yugabyte` user. Ownership and permissions of master and
  tserver data must not change once the database is installed.
- Jinja templates (`*.j2` under `node-agent/resources/`) are rendered in Go with gonja
  (`node-agent/app/task/module/template.go`), and Go code supplies every value:
  - YNP module templates get the module's values from `config.ini` plus what
    `ynp/command/provision_command.go` adds (`os_family`, `templatedir`, ...). `config.ini` is
    rendered from `configs/config.j2` with the JSON that YBA's `YNPConfigGenerator` sends.
  - Node-agent task templates (`templates/server/`) get the map the task in `node-agent/app/task/`
    passes to `CopyFile` or `ResolveTemplate`.

  Rendering is not strict by default: a variable no Go caller sets renders as an empty string, with
  no error. A new or renamed template variable must be set by every caller that renders that
  template, and for YNP also by `config.j2` and `YNPConfigGenerator`. New templates should use
  `ResolveTemplateStrict`. sed and shell in templates must work with GNU tools on RHEL and Ubuntu.
- Go: nil checks, double-closed channels, blocking sends, and leaked child processes or goroutines.

## Java

- `RequestContext` is a `ThreadLocal`. Anything put in it is removed on every path, or it leaks into
  the next request on that Play worker thread.
- Boxed values and strings are compared with `equals`, not `==`.
- `yb-client` calls to a master or tserver that may be stopped have a timeout and handle the error.
  Check whether the call is synchronous or asynchronous, and that its arguments mean what the
  server expects (trace it to the C++ handler).

## UI

- A form value the user can't see must not fail validation, and a value saved by Back must still
  pass validation on Next.
- Data read from the react-query cache can be stale or shared across components; state that used
  to reset on mount may no longer.

## Shell and Python

- Shell: quote variables in destructive commands (`rm`, `mv`, `chmod`, `chown`), and keep scripts
  that run under `sh` POSIX (`echo -n` behaves differently in `sh` and `bash`).
- Python under `devops/` runs from a pex built from `python3_requirements_frozen.txt`. A dependency
  bump must still build and run the pex: setuptools 82 removed `pkg_resources`, for example.

## Generated and build files

- `swagger.json` and `swagger-strict.json` (`sbt swaggerGen`), `RUNTIME-FLAGS.md` (written by
  `SwaggerGenTest` from the `PUBLIC` runtime config keys) and `openapi_public.yaml` (see
  [AGENTS.md](AGENTS.md#where-things-go)) are regenerated from current master and match their
  sources. A stale or partial regeneration silently drops other changes.
- No binaries or archives are committed.

## Platform database migrations (Flyway)

YBA runs Flyway at startup (`YBFlywayInit`) on SQL migrations under
`src/main/resources/db/migration/default_/` and Java migrations under
`src/main/java/db/migration/default_/`, with `outOfOrder=true` and `*:missing` and `*:future`
ignored (`reference.conf`). Flyway validates checksums on every start, so a mistake here stops YBA
from starting on installs that already ran the migration.

- **Never edit a merged versioned migration.** Changing a `V<n>__` SQL file changes its checksum and
  fails startup on every install that applied it. Add a new migration instead. A Java `V<n>__`
  migration has no checksum unless it overrides `getChecksum`, so an edit is not detected: it never
  runs again on installs that already applied it, and only new installs get the fix.
- **Version numbers.** A version already used on master, or by another open PR, collides: two
  migrations with one version fail startup, and once merged the conflict is on master. Rebase and
  take the next free number before merging.
- **Backports.** A backported migration keeps master's version number and content. With
  `outOfOrder=true`, a release-branch install runs it, and a later upgrade to master sees it as
  already applied. A renumbered or edited backport runs twice or fails checksum validation on that
  upgrade. A migration added only on a release branch must take a number master will never use.
- **Java migrations use frozen models.** They read and write through the copies under
  `models/migrations/V<n>` (or raw SQL), not the current Ebean models, which later PRs change.
- **Repeatable migrations** (`R__`) run again whenever their checksum changes; the Java ones compute
  it in `getChecksum`. They must be idempotent and run after any `V` migration, and the inputs to
  their checksum must change exactly when they need to run again.
- **Model and migration match.** An Ebean model change comes with its migration, and a migration
  that rewrites large tables (`audit`, `task_info`) must not lock them for long at startup.
- No local versions (`-SNAPSHOT-test`), test-only commits or debug code at the PR head.
- The PR still builds when merged with current master: a helper that master renamed or moved is a
  compile error the PR's own CI may not show.

## Tests

- A test exercises what it claims. Check mock scopes (a `try (MockedStatic ...)` block that closes
  before the call it was meant to stub) and that the assertions match the stated behavior.
- Existing tests that cover changed methods, such as `*LocalTest` and commissioner task tests, still
  pass and are named in the test plan.
- A bug fix comes with a regression test, or with a reason one is impractical.
