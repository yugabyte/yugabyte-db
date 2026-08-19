package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertGreaterThan;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.IOException;
import java.net.URL;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Properties;
import java.util.Scanner;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicLong;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.LogErrorListener;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.BuildTypeUtil;

/**
 * Reproduces catalog-read amplification caused by cross-route backend churn in
 * multi-route connection pooling, and verifies that the always-on "up-by-two"
 * eviction guard suppresses that churn.
 *
 * <p>When inactive pools hold backends (here pinned via open transactions so
 * they can be neither idle-reaped nor evicted), {@code per_route_quota} in
 * Odyssey shrinks (budget / num_active_routes) while those pools still consume
 * real budget. The churn phase runs two parallel thread pools (one per active pool),
 * each launching K > MAX_CONNECTIONS/2 connections that each hold an explicit
 * transaction, so combined demand exceeds the global budget and forces
 * cross-route eviction; each fresh backend re-fetches non-prefetched catalog
 * entries (pg_statistic, pg_attribute, pg_index) on first plan.
 *
 * <p>With the up-by-two guard (the default, always-on eviction behaviour), an
 * over-quota route may only evict an idle backend from a route that has at least
 * two more connections than itself, which suppresses mutual cross-route
 * eviction, so backends are reused and catalog misses stay near the baseline.
 */
@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestEvictOnlyFromLargerPool extends BaseYsqlConnMgr {

  private static final int MAX_CONNECTIONS = 50;
  private static final int IDLE_PER_INACTIVE = 5;
  // Baseline: single pool, concurrency is per-pool.
  private static final int BASELINE_CONCURRENCY = 25;
  // Churn: each of the two active pools launches K > MAX_CONNECTIONS/2
  // connections simultaneously, so combined demand (2 * K) exceeds the global
  // budget and forces cross-route eviction.
  private static final int CHURN_CONCURRENCY = 50;
  private static final int OPS_PER_TXN = 3;
  // Each worker holds its transaction open for this long after running its
  // queries. Under transaction pooling the backend stays ATTACHED (active,
  // idle-in-transaction) for the whole hold, so it is not returned to the pool
  // as an idle backend. With both active pools oversubscribed (2 * K > budget),
  // this keeps backends occupied and clients queued on both routes, so a backend
  // that briefly frees on commit is more likely to be contested cross-route
  // (a "pull"/eviction) instead of being trivially reused by the same route.
  private static final long TXN_HOLD_MS = 100;
  private static final int ROUNDS = 5;
  private static final int ABS_FLOOR = 30;

  private static final int CHURN_MULTIPLIER = 3;
  private static final int RESERVE_CONNECTIONS = 5;

  private static final String METRIC_TABLE_MISSES =
      "yb_ysqlserver_CatalogCacheTableMisses";
  private static final String METRIC_LIST_MISSES =
      "yb_ysqlserver_CatalogCacheListMisses";

  // Matches the backend-start line emitted by BackendInitialize() in
  // src/postgres/src/backend/postmaster/postmaster.c, e.g.
  //   "Started client backend with pid: 11236, database_name: chdb, ..."
  // Each occurrence corresponds to a freshly spawned physical backend, so
  // counting these lines counts backend spawns.
  private static final Pattern BACKEND_START_PATTERN = Pattern.compile(
      "Started (?<type>[^,]+?) with pid: (?<pid>\\d+),");

  // Accumulates backend-start log lines seen on the tserver(s). Snapshotted
  // before/after each workload run to count how many backends were spawned.
  private final BackendStartCounter backendStartCounter = new BackendStartCounter();

  private static final String WORKLOAD_SQL =
      "SELECT count(*) FROM churn_t WHERE c1 = ? AND c2 < ?";

  // All routes share a single database. Conn-mgr routes are keyed by (db, user)
  // (see od_route_id_compare), so distinct users still yield distinct routes/pools
  // while avoiding a CREATE DATABASE per pool -- by far the slowest setup DDL here.
  private static final String SHARED_DB = "chdb";
  private static final DbUser ACTIVE_A = new DbUser(SHARED_DB, "chu_a");
  private static final DbUser ACTIVE_B = new DbUser(SHARED_DB, "chu_b");
  private static final List<DbUser> ACTIVE_POOL_LIST = Arrays.asList(ACTIVE_A, ACTIVE_B);
  private static final List<DbUser> INACTIVE_POOL_LIST = Arrays.asList(
      new DbUser(SHARED_DB, "chu_i1"),
      new DbUser(SHARED_DB, "chu_i2"),
      new DbUser(SHARED_DB, "chu_i3"));

  private static final List<DbUser> ALL_POOLS;

  static {
    List<DbUser> all = new ArrayList<>();
    all.add(ACTIVE_A);
    all.add(ACTIVE_B);
    all.addAll(INACTIVE_POOL_LIST);
    ALL_POOLS = all;
  }

  // Server idle timeout (pool_ttl) for conn-mgr backends. Kept short so that the
  // backends spawned by the baseline phase are reaped before the churn phase --
  // otherwise ~per_route_quota idle backends linger in ACTIVE_A's pool and eat
  // into the global budget, making the churn phase see an artificially smaller
  // pool. The inactive pools do NOT rely on this timeout: they hold open
  // transactions (see holdTransactionsInPools) so their backends stay attached
  // and are never reaped.
  private static final int SERVER_IDLE_TIME_SEC = 12;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlag("ysql_conn_mgr_enable_multi_route_pool", "true");
    builder.addCommonTServerFlag(
        "ysql_max_connections", String.valueOf(MAX_CONNECTIONS + RESERVE_CONNECTIONS));
    builder.addCommonTServerFlag(
        "ysql_conn_mgr_reserve_internal_conns", String.valueOf(RESERVE_CONNECTIONS));
    builder.addCommonTServerFlag("ysql_conn_mgr_jitter_time", "0");
    builder.addCommonTServerFlag(
        "ysql_conn_mgr_stats_interval", String.valueOf(STATS_UPDATE_INTERVAL));
    builder.addCommonTServerFlag("ysql_enable_auth", "false");
    // Short idle timeout so baseline backends are reaped before churn. Combined
    // with min_pool_size 0 so idle pools can drain fully to zero (no backend is
    // pinned by the minimum-pool floor).
    builder.addCommonTServerFlag(
        "ysql_conn_mgr_idle_time", String.valueOf(SERVER_IDLE_TIME_SEC));
    builder.addCommonTServerFlag("ysql_conn_mgr_min_conns_per_db", "0");
  }

  private static final class DbUser {
    final String database;
    final String user;

    DbUser(String database, String user) {
      this.database = database;
      this.user = user;
    }
  }

  private static final class CatalogMissCounts {
    final long pgStatistic;
    final long pgAttribute;
    final long pgIndex;
    // Number of physical backends spawned (from backend-start log lines). Carried
    // alongside the catalog-miss counts so backend deltas/absolute values are
    // logged wherever the catalog-miss deltas/absolute values are.
    final long backends;

    CatalogMissCounts(long pgStatistic, long pgAttribute, long pgIndex, long backends) {
      this.pgStatistic = pgStatistic;
      this.pgAttribute = pgAttribute;
      this.pgIndex = pgIndex;
      this.backends = backends;
    }

    CatalogMissCounts subtract(CatalogMissCounts other) {
      return new CatalogMissCounts(
          pgStatistic - other.pgStatistic,
          pgAttribute - other.pgAttribute,
          pgIndex - other.pgIndex,
          backends - other.backends);
    }

    @Override
    public String toString() {
      return String.format(
          "pg_statistic=%d pg_attribute=%d pg_index=%d backends=%d",
          pgStatistic, pgAttribute, pgIndex, backends);
    }
  }

  /**
   * Log listener that counts backend-start lines (see {@link #BACKEND_START_PATTERN})
   * seen on a tserver's log stream. The count reflects the number of physical
   * backends spawned. Thread-safe: the {@code LogPrinter} invokes
   * {@link #handleLine} from its own reader thread. {@code LogErrorListener} is a
   * misnomer here -- these are ordinary INFO log lines, not errors.
   */
  private static final class BackendStartCounter implements LogErrorListener {
    private final AtomicLong count = new AtomicLong(0);

    @Override
    public void handleLine(String line) {
      // Cheap pre-filter before the regex.
      if (!line.contains("with pid:") || !line.contains("Started ")) {
        return;
      }
      int idx = line.indexOf("Started ");
      Matcher m = BACKEND_START_PATTERN.matcher(line.substring(idx));
      if (m.find()) {
        count.incrementAndGet();
      }
    }

    @Override
    public void reportErrorsAtEnd() {
      // NOOP
    }

    long total() {
      return count.get();
    }
  }

  private MiniYBDaemon getTserverDaemon() {
    String hostName = getPgHost(TSERVER_IDX);
    for (MiniYBDaemon daemon : miniCluster.getTabletServers().values()) {
      if (hostName.equals(daemon.getLocalhostIP())) {
        return daemon;
      }
    }
    throw new IllegalStateException("Could not find tserver daemon for " + hostName);
  }

  /**
   * Registers {@link #backendStartCounter} on every tserver's log printer so it
   * observes backend-start log lines. Must be called after each cluster
   * (re)start, since a restart creates fresh daemons with new log printers.
   */
  private void attachBackendStartListener() {
    for (MiniYBDaemon tserver : miniCluster.getTabletServers().values()) {
      tserver.getLogPrinter().addErrorListener(backendStartCounter);
    }
  }

  /**
   * Returns the cumulative number of backend spawns observed so far, after a
   * short pause to let the asynchronous log-reader thread catch up with lines
   * emitted by the just-completed workload.
   */
  private long backendsSpawnedSnapshot() throws InterruptedException {
    Thread.sleep(BuildTypeUtil.adjustTimeout(500));
    return backendStartCounter.total();
  }

  private JsonArray fetchYsqlMetricsArray() throws IOException {
    MiniYBDaemon ts = getTserverDaemon();
    String endpoint = String.format(
        "http://%s:%d/metrics", ts.getLocalhostIP(), ts.getPgsqlWebPort());
    LOG.info("Fetching YSQL metrics from {}", endpoint);
    try (Scanner scanner = new Scanner(new URL(endpoint).openConnection().getInputStream())) {
      JsonElement tree = JsonParser.parseString(scanner.useDelimiter("\\A").next());
      return tree.getAsJsonArray().get(0).getAsJsonObject().getAsJsonArray("metrics");
    }
  }

  private long getTableMissCount(JsonArray metrics, String metricName, String tableName) {
    long total = 0;
    for (JsonElement elem : metrics) {
      JsonObject metric = elem.getAsJsonObject();
      if (!metric.has("name") || !metric.has("table_name") || !metric.has("count")) {
        continue;
      }
      if (!metricName.equals(metric.get("name").getAsString())) {
        continue;
      }
      if (!tableName.equals(metric.get("table_name").getAsString())) {
        continue;
      }
      total += metric.get("count").getAsLong();
    }
    return total;
  }

  private CatalogMissCounts snapshotCatalogMisses() throws IOException, InterruptedException {
    JsonArray metrics = fetchYsqlMetricsArray();
    long pgStatistic = getTableMissCount(metrics, METRIC_TABLE_MISSES, "pg_statistic")
        + getTableMissCount(metrics, METRIC_LIST_MISSES, "pg_statistic");
    long pgAttribute = getTableMissCount(metrics, METRIC_TABLE_MISSES, "pg_attribute");
    long pgIndex = getTableMissCount(metrics, METRIC_TABLE_MISSES, "pg_index");
    long backends = backendsSpawnedSnapshot();
    return new CatalogMissCounts(pgStatistic, pgAttribute, pgIndex, backends);
  }

  private Properties workloadConnectionProperties() {
    Properties props = new Properties();
    props.setProperty("prepareThreshold", "1");
    props.setProperty("preparedStatementCacheQueries", "0");
    return props;
  }

  private Connection connectPool(DbUser pool) throws Exception {
    return getConnectionBuilder()
        .withTServer(TSERVER_IDX)
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withDatabase(pool.database)
        .withUser(pool.user)
        .withOptions("-c yb_prefetch_column_statistics=off -c yb_enable_base_scans_cost_model=on")
        .connect(workloadConnectionProperties());
  }

  private void ensureSetup() throws Exception {
    // Run all setup DDL over the direct Postgres endpoint rather than through the
    // pooler: conn-mgr backends sleep for ~2 heartbeat intervals after every DDL
    // commit (see "adding sleep ... after DDL commit" in pg_yb_utils.c) to let
    // catalog invalidations propagate. Direct connections skip that sleep, which
    // removes seconds of pure sleep across the many setup statements.
    try (Connection conn = getConnectionBuilder()
            .withTServer(TSERVER_IDX)
            .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
            .connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute(String.format("DROP DATABASE IF EXISTS %s", SHARED_DB));
      for (DbUser pool : ALL_POOLS) {
        stmt.execute(String.format("DROP USER IF EXISTS %s", pool.user));
        stmt.execute(String.format("CREATE USER %s WITH LOGIN", pool.user));
      }
      stmt.execute(String.format("CREATE DATABASE %s", SHARED_DB));
    }

    // The table lives once in the shared database. Only the active pools ever run
    // the planned workload query; inactive pools just run "SELECT 1" to hold idle
    // backends. Grant SELECT to PUBLIC so every pool's user can read it.
    try (Connection conn = getConnectionBuilder()
            .withTServer(TSERVER_IDX)
            .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
            .withDatabase(SHARED_DB)
            .connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE TABLE churn_t ("
          + "id int PRIMARY KEY, "
          + "c1 int, c2 int, c3 int, c4 int, c5 int)");
      stmt.execute("CREATE INDEX churn_t_c2_idx ON churn_t(c2)");
      stmt.execute("INSERT INTO churn_t SELECT i, i, i * 2, i * 3, i * 4, i * 5 "
          + "FROM generate_series(1, 1000) i");
      stmt.execute("ANALYZE churn_t");
      stmt.execute("GRANT SELECT ON churn_t TO PUBLIC");
    }
  }

  // Only the active pools need to drain between phases. The inactive pools
  // intentionally hold backends (via open transactions) for the whole test, so
  // waiting for them to reach zero would always time out.
  private void waitForPoolsToDrain() throws Exception {
    // Give idle backends time to age past the (short) idle timeout and be
    // reaped, plus a margin for the 1-second reaper tick and stats refresh.
    long deadlineMs = System.currentTimeMillis()
        + (SERVER_IDLE_TIME_SEC + STATS_UPDATE_INTERVAL + 10) * 1000L;
    while (System.currentTimeMillis() < deadlineMs) {
      boolean allClear = true;
      for (DbUser pool : ACTIVE_POOL_LIST) {
        JsonObject poolStats = getPool(pool.database, pool.user);
        if (poolStats == null) {
          continue;
        }
        int active = poolStats.get("active_physical_connections").getAsInt();
        int idle = poolStats.get("idle_physical_connections").getAsInt();
        if (active > 0 || idle > 0) {
          allClear = false;
          break;
        }
      }
      if (allClear) {
        return;
      }
      Thread.sleep(STATS_UPDATE_INTERVAL * 1000L);
    }
    LOG.warn("Pools did not fully drain before next phase; continuing");
  }

  /**
   * Opens {@code count} connections to each pool and holds an explicit
   * transaction open on every one, returning the still-open connections. Under
   * transaction pooling an open transaction keeps its physical backend ATTACHED,
   * so these backends (a) are never returned to the idle server pool and thus
   * cannot be reaped by the idle timeout, and (b) are not eligible for
   * cross-route eviction (which only closes idle servers). This gives the
   * inactive pools a stable set of backends that consume real budget for the
   * whole churn phase, shrinking {@code per_route_quota} for the active routes.
   *
   * <p>The caller owns the returned connections and must close them (see
   * {@link #closeHeldConnections}) once the churn phase is done.
   */
  private List<Connection> holdTransactionsInPools(List<DbUser> pools, int countPerPool)
      throws Exception {
    List<Connection> held = new ArrayList<>();
    try {
      for (DbUser pool : pools) {
        for (int i = 0; i < countPerPool; i++) {
          Connection conn = connectPool(pool);
          conn.setAutoCommit(false);
          // First statement attaches the backend for the whole transaction.
          try (Statement stmt = conn.createStatement()) {
            stmt.execute("SELECT 1");
          }
          held.add(conn);
        }
      }
    } catch (Exception e) {
      closeHeldConnections(held);
      throw e;
    }

    Thread.sleep(STATS_UPDATE_INTERVAL * 1000L);
    for (DbUser pool : pools) {
      JsonObject poolStats = getPool(pool.database, pool.user);
      assertNotNull(poolStats);
      assertGreaterThan(
          "Expected attached (in-transaction) backends in pool "
              + pool.database + "/" + pool.user,
          poolStats.get("active_physical_connections").getAsInt(),
          0);
    }
    return held;
  }

  private void closeHeldConnections(List<Connection> conns) {
    for (Connection conn : conns) {
      if (conn == null) {
        continue;
      }
      try {
        conn.rollback();
      } catch (Exception ignored) {
        // Best-effort; closing below releases the backend regardless.
      }
      try {
        conn.close();
      } catch (Exception ignored) {
        // Best-effort.
      }
    }
  }

  /**
   * A single worker's lifetime: for {@code rounds} iterations, open a
   * connection, run an explicit transaction (which holds a physical backend for
   * its whole duration under transaction pooling), hold it open for
   * {@link #TXN_HOLD_MS} so the backend stays occupied, then commit and close.
   * Running many of these in parallel keeps backends occupied and clients queued
   * on both routes, so freed backends are pulled cross-route rather than reused.
   */
  private void churnWorker(DbUser pool, int rounds) {
    try {
      for (int round = 0; round < rounds; round++) {
        try (Connection conn = connectPool(pool)) {
          // Explicit transaction: the backend stays attached from the first
          // statement until commit, so parallel workers hold backends at once.
          conn.setAutoCommit(false);
          try (PreparedStatement pstmt = conn.prepareStatement(WORKLOAD_SQL)) {
            for (int op = 0; op < OPS_PER_TXN; op++) {
              pstmt.setInt(1, round + op);
              pstmt.setInt(2, 500 + round);
              try (ResultSet rs = pstmt.executeQuery()) {
                assertTrue(rs.next());
              }
            }
          }
          // Hold the transaction open so the backend stays occupied (ACTIVE)
          // and cannot be reused as an idle backend by another client on this
          // route.
          Thread.sleep(TXN_HOLD_MS);
          conn.commit();
        }
      }
    } catch (Exception e) {
      throw new RuntimeException(
          "Churn worker failed for pool " + pool.database + "/" + pool.user, e);
    }
  }

  /**
   * Launches one thread pool per active pool, each firing {@code concurrencyPerPool}
   * workers simultaneously. The main thread does not block on individual
   * connections; it polls pool stats while the workers run, then joins.
   */
  private void runParallelWorkload(
      List<DbUser> pools, int concurrencyPerPool, int rounds, String phase)
      throws Exception {
    List<ExecutorService> executors = new ArrayList<>();
    List<Future<?>> futures = new ArrayList<>();
    try {
      for (DbUser pool : pools) {
        ExecutorService executor = Executors.newFixedThreadPool(concurrencyPerPool);
        executors.add(executor);
        final DbUser workerPool = pool;
        for (int i = 0; i < concurrencyPerPool; i++) {
          futures.add(executor.submit(() -> {
            churnWorker(workerPool, rounds);
            return null;
          }));
        }
      }

      // Non-blocking orchestration: observe the pools while workers churn.
      while (true) {
        long pending = futures.stream().filter(f -> !f.isDone()).count();
        logPoolStats(String.format("%s (in-flight, pending=%d)", phase, pending));
        if (pending == 0) {
          break;
        }
        Thread.sleep(STATS_UPDATE_INTERVAL * 1000L);
      }

      // Propagate any worker failure.
      for (Future<?> future : futures) {
        future.get();
      }
    } finally {
      for (ExecutorService executor : executors) {
        executor.shutdownNow();
      }
    }
  }

  private void logPoolStats(String phase) throws Exception {
    for (DbUser pool : ALL_POOLS) {
      JsonObject poolStats = getPool(pool.database, pool.user);
      if (poolStats == null) {
        LOG.info("{} pool {}/{}: not present", phase, pool.database, pool.user);
        continue;
      }
      LOG.info(
          "{} pool {}/{}: active_physical={} idle_physical={}",
          phase,
          pool.database,
          pool.user,
          poolStats.get("active_physical_connections").getAsInt(),
          poolStats.get("idle_physical_connections").getAsInt());
    }
  }

  private static boolean isAmplified(
      long baselineDelta, long churnDelta) {
    long absoluteGain = churnDelta - baselineDelta;
    return churnDelta > baselineDelta
        && absoluteGain > ABS_FLOOR
        && (churnDelta >= baselineDelta * CHURN_MULTIPLIER
            || (baselineDelta == 0 && churnDelta >= ABS_FLOOR));
  }

  private void assertNoChurnAmplification(
      CatalogMissCounts baselineDelta, CatalogMissCounts churnDelta) {
    LOG.info("Baseline catalog miss delta: {}", baselineDelta);
    LOG.info("Churn catalog miss delta: {}", churnDelta);

    assertCatalogMissNotAmplified(
        "pg_attribute", baselineDelta.pgAttribute, churnDelta.pgAttribute);
    assertCatalogMissNotAmplified(
        "pg_index", baselineDelta.pgIndex, churnDelta.pgIndex);
    assertCatalogMissNotAmplified(
        "pg_statistic", baselineDelta.pgStatistic, churnDelta.pgStatistic);

    // Backend spawns should stay near baseline too: the up-by-two guard keeps an
    // over-quota route from pulling backends from equal- or smaller-sized routes,
    // so backends are reused instead of being churned (re-spawned) cross-route.
    // This is the root cause of the catalog-miss amplification, so asserting on it
    // directly is the strongest check.
    long backendAbsoluteGain = churnDelta.backends - baselineDelta.backends;
    LOG.info(
        "backend spawns (no-churn): baselineDelta={} churnDelta={} absoluteGain={}",
        baselineDelta.backends, churnDelta.backends, backendAbsoluteGain);
    assertFalse(
        "Expected no substantial backend-spawn amplification with the up-by-two "
            + "eviction guard (baseline=" + baselineDelta.backends
            + " churn=" + churnDelta.backends + ")",
        isAmplified(baselineDelta.backends, churnDelta.backends));
  }

  private void assertCatalogMissNotAmplified(
      String tableName, long baselineDelta, long churnDelta) {
    long absoluteGain = churnDelta - baselineDelta;
    LOG.info(
        "{} misses (no-churn): baselineDelta={} churnDelta={} absoluteGain={}",
        tableName, baselineDelta, churnDelta, absoluteGain);
    assertFalse(
        "Expected no substantial " + tableName + " catalog-miss amplification with the "
            + "up-by-two eviction guard (baseline=" + baselineDelta
            + " churn=" + churnDelta + ")",
        isAmplified(baselineDelta, churnDelta));
  }

  @Test
  public void testCatalogReadAmplificationUnderBackendChurn() throws Exception {
    attachBackendStartListener();

    ensureSetup();
    waitForPoolsToDrain();

    CatalogMissCounts baselineBefore = snapshotCatalogMisses();
    runParallelWorkload(Arrays.asList(ACTIVE_A), BASELINE_CONCURRENCY, ROUNDS, "baseline");
    CatalogMissCounts baselineAfter = snapshotCatalogMisses();
    CatalogMissCounts baselineDelta = baselineAfter.subtract(baselineBefore);
    logPoolStats("baseline");

    // Drain first so ACTIVE_A's baseline backends are reaped (short idle timeout)
    // and don't linger to eat into the churn phase's global budget.
    waitForPoolsToDrain();

    // Hold open transactions in the inactive pools for the whole churn phase.
    // Their backends stay attached, so they can be neither idle-reaped nor
    // cross-route evicted, giving stable budget pressure that shrinks
    // per_route_quota for the active routes.
    List<Connection> heldInactiveConns =
        holdTransactionsInPools(INACTIVE_POOL_LIST, IDLE_PER_INACTIVE);
    try {
      logPoolStats("after inactive hold");

      CatalogMissCounts churnBefore = snapshotCatalogMisses();
      runParallelWorkload(
          Arrays.asList(ACTIVE_A, ACTIVE_B), CHURN_CONCURRENCY, ROUNDS, "churn");
      CatalogMissCounts churnAfter = snapshotCatalogMisses();
      CatalogMissCounts churnDelta = churnAfter.subtract(churnBefore);
      logPoolStats("churn");

      assertNoChurnAmplification(baselineDelta, churnDelta);
    } finally {
      closeHeldConnections(heldInactiveConns);
    }
  }
}
