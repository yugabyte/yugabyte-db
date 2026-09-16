// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import io.zonky.test.db.postgres.embedded.EmbeddedPostgres;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.time.Duration;
import java.util.Properties;
import java.util.concurrent.CountDownLatch;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * A single embedded PostgreSQL server, started once for a whole test run and shared by every forked
 * test JVM.
 *
 * <p>Historically every forked test JVM started its own embedded Postgres and ran the full flyway
 * migration on its first application build. With ~17 shards that meant ~17 server starts and ~17
 * full migrations. Instead, sbt now launches this class as a dedicated, long-lived JVM (via {@code
 * Tests.Setup}) before the test task; each fork connects to it over TCP, the very first fork
 * migrates a {@code yba_template} database once, and every fork clones its own working database
 * from that template with {@code CREATE DATABASE ... WITH TEMPLATE} (a cheap server-side file copy)
 * - so the expensive migration runs exactly once per run. See {@link TestPostgres} for the fork
 * side.
 *
 * <p>It runs in its own JVM (not the sbt JVM and not a fork) for two reasons: the sbt JVM would
 * need an isolated classloader to see the test classpath (fragile, and it mis-detects the CPU
 * architecture, falling back to slow x86_64/Rosetta binaries), whereas a plain forked JVM starts
 * embedded Postgres exactly like the test forks do. The owning JVM is spawned/killed by {@code
 * SharedPgControl} in build.sbt; on {@code SIGTERM} its shutdown hook stops the server cleanly.
 */
public final class SharedEmbeddedPostgres {

  private static final Logger LOG = LoggerFactory.getLogger(SharedEmbeddedPostgres.class);

  // The shared server must comfortably hold the connection pools of every parallel fork
  // (testParallelForks * (db.default + db.perf_advisor pools)) plus the transient migrator app and
  // maintenance connections. The Postgres default of 100 is not enough, so raise it.
  private static final String MAX_CONNECTIONS = "500";

  private static final String PROP_HOST = "host";
  private static final String PROP_PORT = "port";
  private static final String PROP_SUPERUSER = "superuser";
  private static final String PROP_PID = "pid";

  private static volatile EmbeddedPostgres pg;

  private SharedEmbeddedPostgres() {}

  /**
   * Entry point of the dedicated owning JVM: start the server, publish its connection details, then
   * park until this JVM is killed (by build.sbt's Tests.Cleanup), at which point the shutdown hook
   * stops the server cleanly.
   */
  public static void main(String[] args) throws Exception {
    if (args.length < 1) {
      throw new IllegalArgumentException("Usage: SharedEmbeddedPostgres <confPath>");
    }
    start(args[0]);
    // Block forever; the process is terminated by Tests.Cleanup.
    new CountDownLatch(1).await();
  }

  private static void start(String confPath) throws Exception {
    File confFile = new File(confPath);
    // Leftover conf from a previous run of THIS suite (e.g. sbt killed with Ctrl-C) is stale now
    // that we own a fresh server; remove it so forks never read a dead server's details.
    confFile.delete();
    LOG.info("Starting shared embedded postgres for the test run");
    pg =
        EmbeddedPostgres.builder()
            .setPGStartupWait(Duration.ofSeconds(120))
            .setServerConfig("max_connections", MAX_CONNECTIONS)
            .start();
    long pid = ProcessHandle.current().pid();
    Runtime.getRuntime()
        .addShutdownHook(
            new Thread("Stop shared embedded postgres") {
              @Override
              public void run() {
                try {
                  pg.close();
                } catch (Exception e) {
                  LOG.warn("Failed to stop shared embedded postgres", e);
                }
                confFile.delete();
              }
            });

    Properties props = new Properties();
    props.setProperty(PROP_HOST, "localhost");
    props.setProperty(PROP_PORT, Integer.toString(pg.getPort()));
    // Zonky always provisions the "postgres" superuser with trust auth on loopback.
    props.setProperty(PROP_SUPERUSER, "postgres");
    props.setProperty(PROP_PID, Long.toString(pid));
    writeAtomically(confFile, props);
    LOG.info("Shared embedded postgres ready on port {} (owner pid {})", pg.getPort(), pid);
  }

  // Write to a temp file and atomically rename so a fork never reads a half-written conf and the
  // conf's existence is a reliable readiness signal for the launcher.
  private static void writeAtomically(File confFile, Properties props) throws Exception {
    File parent = confFile.getParentFile();
    if (parent != null) {
      parent.mkdirs();
    }
    File tmp = File.createTempFile("shared-pg", ".tmp", parent);
    try (FileOutputStream out = new FileOutputStream(tmp)) {
      props.store(out, "Shared embedded postgres for the test run");
    }
    Files.move(tmp.toPath(), confFile.toPath(), StandardCopyOption.REPLACE_EXISTING);
  }
}
