// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableList;
import io.zonky.test.db.postgres.embedded.EmbeddedPostgres;
import java.io.File;
import java.io.FileInputStream;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.concurrent.atomic.AtomicInteger;
import javax.sql.DataSource;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Application;
import play.test.Helpers;

/**
 * Provides a migrated PostgreSQL database to each test.
 *
 * <p>There are two modes, selected automatically:
 *
 * <ul>
 *   <li><b>Shared mode</b> (a full {@code sbt test} run). sbt starts <b>one</b> embedded Postgres
 *       for the whole run (see {@link SharedEmbeddedPostgres}) and passes its connection file to
 *       every forked JVM via the {@code yb.test.sharedPgConf} system property. The very first fork
 *       to grab a Postgres advisory lock creates a {@code yba_template} database and migrates it
 *       once (by building a throwaway real application so Ebean-based migrations work); it is then
 *       frozen ({@code datistemplate=true, datallowconn=false}). Every fork clones its own working
 *       database from that template with {@code CREATE DATABASE ... WITH TEMPLATE} (a cheap
 *       server-side file copy), so neither Postgres startup nor the flyway migration is paid more
 *       than once per run.
 *   <li><b>Embedded fallback</b> (IDE / running a single class without the sbt wrapper). Each JVM
 *       starts its own embedded Postgres and the first application build migrates it, exactly as
 *       before. Teardown is made bulletproof so servers never leak: a shutdown hook stops the
 *       server on clean exit and {@link #reapOrphanedEmbeddedPostgres()} kills any orphan left by a
 *       JVM that died hard before starting a fresh one.
 * </ul>
 *
 * <p>In both modes, state is reset between test methods by truncating every table (see {@link
 * #resetDatabase()}) rather than dropping/recreating the schema, and each forked JVM is fully
 * isolated from the others (its own working database in shared mode, its own server in embedded
 * mode).
 */
public final class TestPostgres {

  private static final Logger LOG = LoggerFactory.getLogger(TestPostgres.class);

  // System property (set by build.sbt on every forked test JVM) pointing at the connection file of
  // the single shared server. Absent for IDE / non-sbt runs, which then use the embedded fallback.
  private static final String SHARED_CONF_PROP = "yb.test.sharedPgConf";

  // The migrated-once database that every fork's working database is cloned from.
  private static final String TEMPLATE_DB = "yba_template";

  // A fixed key so all forks serialize template creation on the same Postgres advisory lock.
  private static final long TEMPLATE_LOCK_KEY = 6034552198765432101L;

  // Substring present in the command line of every embedded postgres (both the postgres binary path
  // and the -D data directory live under this temp dir). Used to distinguish Zonky's postmaster
  // from
  // any real local postgres a developer may be running, so the reaper never touches the latter.
  private static final String EMBEDDED_PG_MARKER = "embedded-pg";

  // Global runtime config scope row seeded by migration V60. It is the only non customer/universe
  // scoped baseline row present right after migrating an empty database, so we re-create it after
  // every truncate to keep the reset state identical to a freshly migrated database.
  private static final String GLOBAL_SCOPE_UUID = "00000000-0000-0000-0000-000000000000";

  private static final String SCHEMA_VERSION_TABLE = "schema_version";

  // Embedded-fallback only.
  private static volatile EmbeddedPostgres pg;
  private static volatile DataSource dataSource;

  // Shared mode only.
  private static volatile boolean sharedMode = false;
  private static volatile String sharedHost;
  private static volatile int sharedPort;
  private static volatile String sharedSuperuser;
  private static volatile String workingDbName;

  // The working database this JVM's applications connect to (in both modes).
  private static volatile String jdbcUrl;
  private static volatile String username;

  private static volatile long postmasterPid = -1;
  private static volatile boolean started = false;
  private static volatile boolean initializing = false;

  private static final AtomicInteger ISOLATED_DB_SEQ = new AtomicInteger();

  private TestPostgres() {}

  public static synchronized void ensureStarted() {
    if (started || initializing) {
      return;
    }
    initializing = true;
    try {
      String confPath = System.getProperty(SHARED_CONF_PROP);
      if (confPath != null && new File(confPath).isFile()) {
        initShared(confPath);
      } else {
        initEmbedded();
      }
      started = true;
    } finally {
      initializing = false;
    }
  }

  // ---------------------------------------------------------------------------
  // Shared mode
  // ---------------------------------------------------------------------------

  private static void initShared(String confPath) {
    sharedMode = true;
    Properties props = new Properties();
    try (FileInputStream in = new FileInputStream(confPath)) {
      props.load(in);
    } catch (Exception e) {
      throw new RuntimeException("Failed to read shared postgres config " + confPath, e);
    }
    sharedHost = props.getProperty("host", "localhost");
    sharedPort = Integer.parseInt(props.getProperty("port"));
    sharedSuperuser = props.getProperty("superuser", "postgres");

    ensureTemplateReady();

    // A distinct working database per forked JVM keeps parallel shards isolated (mirroring the old
    // one-embedded-server-per-JVM model). The pid is unique per live JVM; drop any leftover first
    // in case the server outlived a crashed JVM whose pid got reused.
    workingDbName = "test_fork_" + ProcessHandle.current().pid();
    createDatabaseFromTemplate(workingDbName);
    jdbcUrl = urlFor(workingDbName);
    username = sharedSuperuser;
    LOG.info("Fork using shared postgres working database {}", workingDbName);
  }

  private static void ensureTemplateReady() {
    try (Connection connection = connectShared("postgres")) {
      connection.setAutoCommit(true);
      execute(connection, "SELECT pg_advisory_lock(" + TEMPLATE_LOCK_KEY + ")");
      try {
        if (!isTemplateReady(connection)) {
          buildTemplate(connection);
        }
      } finally {
        execute(connection, "SELECT pg_advisory_unlock(" + TEMPLATE_LOCK_KEY + ")");
      }
    } catch (SQLException e) {
      throw new RuntimeException("Failed to prepare shared postgres template database", e);
    }
  }

  private static boolean isTemplateReady(Connection connection) throws SQLException {
    try (Statement statement = connection.createStatement();
        ResultSet rs =
            statement.executeQuery(
                "SELECT datistemplate FROM pg_database WHERE datname = '" + TEMPLATE_DB + "'")) {
      return rs.next() && rs.getBoolean(1);
    }
  }

  private static void buildTemplate(Connection connection) throws SQLException {
    LOG.info("Migrating shared postgres template database {} (once per run)", TEMPLATE_DB);
    // Drop any partial template from a previous, interrupted attempt. A database flagged as a
    // template cannot be dropped, so clear the flag first, and terminate any stale backends.
    execute(
        connection,
        "UPDATE pg_database SET datistemplate = false WHERE datname = '" + TEMPLATE_DB + "'");
    terminateConnections(connection, TEMPLATE_DB);
    execute(connection, "DROP DATABASE IF EXISTS " + TEMPLATE_DB);
    execute(connection, "CREATE DATABASE " + TEMPLATE_DB);

    // pgcrypto is required by several postgres migrations (pgp_sym_encrypt / pgp_sym_decrypt) and
    // by @Encrypted Ebean columns. It is normally provisioned by YBA installers. It is created
    // per-database, so it must exist in the template before migrations run (and is inherited by
    // every clone).
    try (Connection templateConn = connectShared(TEMPLATE_DB)) {
      execute(templateConn, "CREATE EXTENSION IF NOT EXISTS pgcrypto");
    }

    // Migrate through a real application so migrations that use Ebean's default server (e.g.
    // R__Sync_System_Roles) hit the template. buildMigrationApp bypasses TestHelper.testDatabase()
    // to avoid reentering ensureStarted() while we are still initializing.
    Application migrator = FakeDBApplication.buildMigrationApp(templateMigrationConfig());
    try {
      Helpers.stop(migrator);
    } finally {
      // Make sure Ebean's globally-named "default"/"perf_advisor" servers (pointed at the template
      // during migration) are shut down so the fork's real working application can register fresh
      // ones, and so no connection lingers on the template (required to freeze/clone it).
      try {
        TestHelper.shutdownDatabase();
      } catch (RuntimeException ignore) {
        // Already shut down by application stop - nothing else to do.
      }
    }

    // Freeze the template: no connections allowed (so it can be safely used as a CREATE DATABASE
    // source), and marked as a template.
    terminateConnections(connection, TEMPLATE_DB);
    execute(
        connection,
        "UPDATE pg_database SET datistemplate = true, datallowconn = false WHERE datname = '"
            + TEMPLATE_DB
            + "'");
    LOG.info("Shared postgres template database {} is ready", TEMPLATE_DB);
  }

  private static void createDatabaseFromTemplate(String dbName) {
    try (Connection connection = connectShared("postgres")) {
      connection.setAutoCommit(true);
      terminateConnections(connection, dbName);
      execute(connection, "DROP DATABASE IF EXISTS \"" + dbName + "\"");
      execute(connection, "CREATE DATABASE \"" + dbName + "\" TEMPLATE " + TEMPLATE_DB);
    } catch (SQLException e) {
      throw new RuntimeException("Failed to clone database " + dbName + " from template", e);
    }
  }

  private static Map<String, Object> templateMigrationConfig() {
    Map<String, Object> config = new HashMap<>();
    config.put("db.default.driver", "org.postgresql.Driver");
    config.put("db.default.url", urlFor(TEMPLATE_DB));
    config.put("db.default.username", sharedSuperuser);
    config.put("db.default.password", getPassword());
    config.put("db.default.migration.locations", ImmutableList.of("common", "postgres"));
    config.put("db.default.migration.auto", true);
    config.put("db.perf_advisor.driver", "org.postgresql.Driver");
    config.put("db.perf_advisor.url", urlFor(TEMPLATE_DB));
    config.put("db.perf_advisor.username", sharedSuperuser);
    config.put("db.perf_advisor.password", getPassword());
    // Only the default schema belongs in the template; never let flyway apply perf_advisor
    // migrations to it.
    config.put("db.perf_advisor.migration.auto", false);
    return config;
  }

  private static void terminateConnections(Connection connection, String dbName)
      throws SQLException {
    execute(
        connection,
        "SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE datname = '"
            + dbName
            + "' AND pid <> pg_backend_pid()");
  }

  private static Connection connectShared(String dbName) throws SQLException {
    return DriverManager.getConnection(urlFor(dbName), sharedSuperuser, getPassword());
  }

  private static String urlFor(String dbName) {
    return "jdbc:postgresql://" + sharedHost + ":" + sharedPort + "/" + dbName;
  }

  private static void execute(Connection connection, String sql) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      statement.execute(sql);
    }
  }

  // ---------------------------------------------------------------------------
  // Embedded fallback mode
  // ---------------------------------------------------------------------------

  private static void initEmbedded() {
    // Kill any embedded postgres left behind by a previously crashed/killed test JVM before we add
    // one more server to the machine. This bounds the live-server count and prevents the resource
    // exhaustion (fork/semaphore starvation) that manifests as a fresh postmaster hanging in JDBC
    // authentication forever.
    reapOrphanedEmbeddedPostgres();
    try {
      // Zonky's default readiness wait is 10s. Under a loaded machine (many parallel forked JVMs)
      // or
      // when a profiler agent is attached (signal-based sampling slows the initdb/startup
      // syscalls),
      // a healthy postmaster can take longer than that, causing spurious "Gave up waiting for
      // server
      // to start" failures that fail the whole shard. Give it generous headroom.
      pg = EmbeddedPostgres.builder().setPGStartupWait(java.time.Duration.ofSeconds(60)).start();
      postmasterPid = findOwnPostmasterPid();
      dataSource = pg.getPostgresDatabase();
      try (Connection connection = dataSource.getConnection()) {
        jdbcUrl = connection.getMetaData().getURL();
        username = connection.getMetaData().getUserName();
        try (Statement statement = connection.createStatement()) {
          statement.execute("CREATE EXTENSION IF NOT EXISTS pgcrypto");
        }
      }
      Runtime.getRuntime()
          .addShutdownHook(
              new Thread("Stop embedded postgres") {
                @Override
                public void run() {
                  try {
                    pg.close();
                  } catch (Exception e) {
                    LOG.warn("Failed to stop embedded postgres", e);
                  }
                  // Belt and suspenders: if pg.close() failed or timed out, make sure the
                  // postmaster
                  // process (and, transitively, its child backends) is gone so it does not leak.
                  if (postmasterPid > 0) {
                    ProcessHandle.of(postmasterPid)
                        .filter(ProcessHandle::isAlive)
                        .ifPresent(ProcessHandle::destroyForcibly);
                  }
                }
              });
    } catch (Exception e) {
      throw new RuntimeException("Failed to start embedded postgres", e);
    }
  }

  /**
   * Kill every embedded postgres postmaster that has been orphaned (reparented to init, pid 1)
   * because the test JVM that owned it exited without running its shutdown hook. Best-effort: any
   * failure here must never fail a test, so exceptions are only logged.
   */
  private static void reapOrphanedEmbeddedPostgres() {
    try {
      long selfPid = ProcessHandle.current().pid();
      ProcessHandle.allProcesses()
          .filter(ph -> ph.pid() != selfPid)
          .filter(TestPostgres::isEmbeddedPostgresPostmaster)
          .filter(TestPostgres::isOrphaned)
          .forEach(
              ph -> {
                LOG.warn(
                    "Reaping orphaned embedded postgres (pid={}) left over from a dead test JVM",
                    ph.pid());
                ph.destroyForcibly();
              });
    } catch (RuntimeException e) {
      LOG.warn("Failed to reap orphaned embedded postgres processes", e);
    }
  }

  // The postmaster started by this JVM is a direct child of it (Zonky spawns postgres and pipes its
  // output), so it is the embedded postgres whose parent is us.
  private static long findOwnPostmasterPid() {
    long selfPid = ProcessHandle.current().pid();
    return ProcessHandle.allProcesses()
        .filter(ph -> ph.parent().map(ProcessHandle::pid).orElse(-1L) == selfPid)
        .filter(TestPostgres::isEmbeddedPostgresPostmaster)
        .mapToLong(ProcessHandle::pid)
        .findFirst()
        .orElse(-1L);
  }

  private static boolean isEmbeddedPostgresPostmaster(ProcessHandle ph) {
    ProcessHandle.Info info = ph.info();
    String cmdLine = info.commandLine().orElse("");
    if (cmdLine.isEmpty()) {
      // commandLine() can be empty when the OS hides args; fall back to command + arguments.
      String command = info.command().orElse("");
      String args = String.join(" ", info.arguments().orElse(new String[0]));
      cmdLine = command + " " + args;
    }
    // Only the Zonky postmaster (postgres launched with -D against its temp data dir). Child
    // backends
    // (e.g. "postgres: checkpointer") do not carry -D and die with the postmaster anyway.
    return cmdLine.contains(EMBEDDED_PG_MARKER)
        && cmdLine.contains("postgres")
        && cmdLine.contains("-D");
  }

  // A live sibling test JVM's postgres has that JVM as its parent; once the JVM dies the postgres
  // is
  // reparented to init (pid 1). Treat an absent parent as orphaned too.
  private static boolean isOrphaned(ProcessHandle ph) {
    return ph.parent().map(ProcessHandle::pid).orElse(1L) == 1L;
  }

  // ---------------------------------------------------------------------------
  // Common API (works in both modes)
  // ---------------------------------------------------------------------------

  public static boolean isStarted() {
    return started;
  }

  private static Connection openWorkingConnection() throws SQLException {
    if (sharedMode) {
      return DriverManager.getConnection(jdbcUrl, username, getPassword());
    }
    return dataSource.getConnection();
  }

  /**
   * True once the schema has been migrated (i.e. flyway created its history table). Derived
   * directly from the live working database rather than an in-JVM flag so it is correct for every
   * test regardless of whether it goes through {@link PlatformGuiceApplicationBaseTest} - some
   * tests (e.g. SessionControllerTest) build their own application and never touch the base class.
   */
  public static boolean isSchemaMigrated() {
    if (!started) {
      return false;
    }
    try (Connection connection = openWorkingConnection();
        Statement statement = connection.createStatement();
        ResultSet rs =
            statement.executeQuery(
                "SELECT to_regclass('public." + SCHEMA_VERSION_TABLE + "') IS NOT NULL")) {
      return rs.next() && rs.getBoolean(1);
    } catch (SQLException e) {
      throw new RuntimeException("Failed to check embedded postgres schema", e);
    }
  }

  public static String getJdbcUrl() {
    ensureStarted();
    return jdbcUrl;
  }

  public static String getUsername() {
    ensureStarted();
    return username;
  }

  public static String getPassword() {
    return "";
  }

  /**
   * Creates a brand new, migrated database that is fully independent of this JVM's working database
   * and returns Play "db.default.*" configuration pointing at it.
   *
   * <p>A handful of tests build a second application in the same method to emulate a second, fully
   * independent YBA instance (e.g. {@code PlatformTest} - a leader and a follower). With H2 every
   * application got its own randomly-named in-memory database, so those two applications were
   * naturally isolated. Both our modes serve one physical server per JVM, so such tests must ask
   * for an isolated database explicitly for the secondary application to avoid stepping on the
   * primary's data (e.g. the singleton HA config row).
   *
   * <p>In shared mode the isolated database is cloned from the already-migrated template (so no
   * flyway run is needed); in embedded mode it is created empty and the application migrates it.
   */
  public static synchronized Map<String, Object> newIsolatedDatabaseConfig() {
    ensureStarted();
    String dbName = "isolated_test_" + ISOLATED_DB_SEQ.incrementAndGet();
    if (sharedMode) {
      createDatabaseFromTemplate(dbName);
      Map<String, Object> config = new HashMap<>();
      config.put("db.default.driver", "org.postgresql.Driver");
      config.put("db.default.url", urlFor(dbName));
      config.put("db.default.username", sharedSuperuser);
      config.put("db.default.password", getPassword());
      config.put("db.default.migration.locations", ImmutableList.of("common", "postgres"));
      // Cloned from the migrated template - already has the schema.
      config.put("db.default.migration.auto", false);
      return config;
    }
    try (Connection connection = dataSource.getConnection();
        Statement statement = connection.createStatement()) {
      statement.execute("CREATE DATABASE " + dbName);
    } catch (SQLException e) {
      throw new RuntimeException("Failed to create isolated database " + dbName, e);
    }
    // pgcrypto is created per-database, so the fresh database needs its own copy before migrations
    // (which use pgp_sym_encrypt/pgp_sym_decrypt) run.
    try (Connection connection = pg.getDatabase(username, dbName).getConnection();
        Statement statement = connection.createStatement()) {
      statement.execute("CREATE EXTENSION IF NOT EXISTS pgcrypto");
    } catch (SQLException e) {
      throw new RuntimeException("Failed to prepare isolated database " + dbName, e);
    }
    Map<String, Object> config = new HashMap<>();
    config.put("db.default.driver", "org.postgresql.Driver");
    config.put("db.default.url", pg.getJdbcUrl(username, dbName));
    config.put("db.default.username", username);
    config.put("db.default.password", getPassword());
    config.put("db.default.migration.locations", ImmutableList.of("common", "postgres"));
    // A brand new database always needs migrating.
    config.put("db.default.migration.auto", true);
    return config;
  }

  /**
   * Reset every table to an empty state and reseed the global runtime config scope so that the
   * database looks exactly like a freshly migrated one. Cheap enough to run before every test
   * method. No-op until the schema has been migrated (getTruncateSql returns null when there are no
   * user tables yet).
   */
  public static void resetDatabase() {
    if (!started) {
      return;
    }
    try (Connection connection = openWorkingConnection();
        Statement statement = connection.createStatement()) {
      String sql = getTruncateSql(connection);
      if (sql == null) {
        return;
      }
      statement.execute(sql);
      // TRUNCATE ... RESTART IDENTITY only resets sequences OWNED BY a truncated column. Many Ebean
      // models generate ids from a plain (non-owned) sequence (e.g. Audit's audit_id_seq), which
      // would otherwise keep incrementing across the test methods that share this database. Reset
      // every sequence so the database looks exactly like a freshly migrated one.
      String resetSequences = getResetSequencesSql(connection);
      if (resetSequences != null) {
        statement.execute(resetSequences);
      }
      statement.execute(
          "INSERT INTO scoped_runtime_config (uuid) VALUES ('" + GLOBAL_SCOPE_UUID + "')");
    } catch (SQLException e) {
      throw new RuntimeException("Failed to reset embedded postgres", e);
    }
  }

  // The set of tables is recomputed from the live database on every reset rather than cached: a
  // single JVM may build several applications and caching a stale table list can reference tables
  // that no longer exist (or miss newly created ones), causing TRUNCATE to fail.
  private static String getTruncateSql(Connection connection) throws SQLException {
    List<String> tables = new ArrayList<>();
    try (Statement statement = connection.createStatement();
        ResultSet rs =
            statement.executeQuery(
                "SELECT tablename FROM pg_tables WHERE schemaname = 'public' AND tablename <> '"
                    + SCHEMA_VERSION_TABLE
                    + "'")) {
      while (rs.next()) {
        tables.add('"' + rs.getString(1) + '"');
      }
    }
    if (tables.isEmpty()) {
      return null;
    }
    return "TRUNCATE TABLE " + String.join(", ", tables) + " RESTART IDENTITY CASCADE";
  }

  // Builds a single batched statement that restarts every sequence in the public schema back to its
  // start value. The Postgres JDBC driver executes semicolon-separated commands in one round trip.
  private static String getResetSequencesSql(Connection connection) throws SQLException {
    StringBuilder sb = new StringBuilder();
    try (Statement statement = connection.createStatement();
        ResultSet rs =
            statement.executeQuery(
                "SELECT sequencename FROM pg_sequences WHERE schemaname = 'public'")) {
      while (rs.next()) {
        sb.append("ALTER SEQUENCE public.\"").append(rs.getString(1)).append("\" RESTART;");
      }
    }
    return sb.length() == 0 ? null : sb.toString();
  }
}
