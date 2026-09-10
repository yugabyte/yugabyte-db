// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.List;
import java.util.Random;
import java.util.UUID;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SimpleSqlPayload {
  private static final String TABLE_NAME = "sql_payload_table";
  private static final long TOPOLOGY_REFRESH_MILLIS = 1000;
  private AtomicInteger lastId = new AtomicInteger(1);
  private int readThreads;
  private int writeThreads;
  private long timeBetweenRetries;
  private UUID universeUuid;

  private AtomicInteger errorCount = new AtomicInteger();
  private AtomicInteger readCount = new AtomicInteger();
  private AtomicInteger writeCount = new AtomicInteger();

  private volatile boolean stopped = false;

  public SimpleSqlPayload(
      int readThreads, int writeThreads, int timeBetweenRetries, Universe universe) {
    this.readThreads = readThreads;
    this.writeThreads = writeThreads;
    this.timeBetweenRetries = timeBetweenRetries;
    this.universeUuid = universe.getUniverseUUID();
  }

  private ExecutorService executor;
  private volatile HikariDataSource dataSource;
  private volatile String currentHosts;
  private Long startTime;

  public void init() {
    currentHosts = liveHosts();
    dataSource = createDataSource(currentHosts);

    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("SimplePayload-%d").build();
    executor = Executors.newCachedThreadPool(namedThreadFactory);
  }

  private HikariDataSource createDataSource(String hosts) {
    HikariConfig config = new HikariConfig();
    config.setJdbcUrl("jdbc:postgresql://" + hosts + "/" + Util.YUGABYTE_DB);
    config.setUsername("yugabyte");
    config.setPassword("");
    config.setConnectionTimeout(5 * 1000);
    config.setValidationTimeout(1000);
    config.setIdleTimeout(10 * 60 * 1000);
    config.setMaxLifetime(30 * 60 * 1000);
    return new HikariDataSource(config);
  }

  private String liveHosts() {
    Universe current = Universe.getOrBadRequest(universeUuid);
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        current.getUniverseDetails().getPrimaryCluster();
    List<NodeDetails> reachable =
        current.getNodesInCluster(primaryCluster.uuid).stream()
            .filter(n -> n.isTserver && n.cloudInfo != null && n.cloudInfo.private_ip != null)
            .collect(Collectors.toList());
    List<NodeDetails> live =
        reachable.stream()
            .filter(n -> n.state == NodeDetails.NodeState.Live)
            .collect(Collectors.toList());
    return (live.isEmpty() ? reachable : live)
        .stream()
            .map(n -> n.cloudInfo.private_ip + ":" + n.ysqlServerRpcPort)
            .collect(Collectors.joining(","));
  }

  /**
   * A real client discovers topology changes; without this the payload keeps dialing the nodes that
   * existed when it started and counts every request against a node the test itself removed as an
   * error.
   */
  private void refreshTopology() {
    String hosts;
    try {
      hosts = liveHosts();
    } catch (Exception e) {
      log.warn("Failed to read universe topology", e);
      return;
    }
    if (hosts.isEmpty() || hosts.equals(currentHosts)) {
      return;
    }
    log.info("Payload topology changed from [{}] to [{}]", currentHosts, hosts);
    HikariDataSource previous = dataSource;
    dataSource = createDataSource(hosts);
    currentHosts = hosts;
    // Give in-flight statements a chance to finish on the old pool: closing it under them would
    // produce exactly the errors this refresh exists to avoid.
    executor.submit(
        () -> {
          try {
            Thread.sleep(5000);
          } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
          }
          previous.close();
        });
  }

  public void start() {
    startTime = System.currentTimeMillis();
    try {
      createTable();
    } catch (SQLException e) {
      throw new RuntimeException(e);
    }
    executor.submit(
        () -> {
          while (!stopped) {
            refreshTopology();
            try {
              Thread.sleep(TOPOLOGY_REFRESH_MILLIS);
            } catch (InterruptedException e) {
              Thread.currentThread().interrupt();
              return;
            }
          }
        });
    for (int i = 0; i < readThreads; i++) {
      startThread(this::readFromTable);
    }
    for (int i = 0; i < writeThreads; i++) {
      startThread(
          () -> {
            if (new Random().nextInt(5) == 1) {
              deleteFromTable();
            } else {
              insertIntoTable();
            }
            return null;
          });
    }
  }

  public void stop() {
    stopped = true;
    if (executor != null) {
      executor.shutdown();
    }
    HikariDataSource current = dataSource;
    if (current != null) {
      current.close();
    }
  }

  public double getErrorPercent() {
    return ((double) errorCount.get() * 100) / (readCount.get() + writeCount.get());
  }

  private void startThread(Callable<?> action) {
    executor.submit(
        () -> {
          while (!stopped) {
            try {
              action.call();
            } catch (Exception e) {
              if (!stopped) {
                log.error("Received error", e);
                errorCount.incrementAndGet();
              }
            }
            try {
              if (!stopped) {
                Thread.sleep(new Random().nextLong(timeBetweenRetries));
              }
            } catch (InterruptedException e) {
            }
          }
        });
  }

  private void createTable() throws SQLException {
    try (Connection connection = dataSource.getConnection()) {
      PreparedStatement ps =
          connection.prepareStatement(
              "CREATE TABLE IF NOT EXISTS "
                  + TABLE_NAME
                  + " (id int, name text, age int, PRIMARY KEY(id, name))");
      ps.execute();
    }
  }

  private boolean readFromTable() throws SQLException {
    try (Connection connection = dataSource.getConnection()) {
      PreparedStatement ps =
          connection.prepareStatement(
              "select * from " + TABLE_NAME + " order by random() limit 10");
      ResultSet resultSet = ps.executeQuery();
      while (resultSet.next()) {
        int id = resultSet.getInt(1);
        readCount.incrementAndGet();
      }
    }
    return true;
  }

  private boolean deleteFromTable() throws SQLException {
    try (Connection connection = dataSource.getConnection()) {
      PreparedStatement ps =
          connection.prepareStatement(
              "delete from "
                  + TABLE_NAME
                  + " where id in "
                  + "(select id from "
                  + TABLE_NAME
                  + " order by random() limit 5)");
      ps.executeUpdate();
    }
    return true;
  }

  private boolean insertIntoTable() throws SQLException {
    try (Connection connection = dataSource.getConnection()) {
      PreparedStatement ps =
          connection.prepareStatement("insert into " + TABLE_NAME + " values (?, ?)");
      ps.setInt(1, lastId.incrementAndGet());
      ps.setString(2, "Name" + System.currentTimeMillis());
      ps.executeUpdate();
      writeCount.incrementAndGet();
    }
    return true;
  }
}
