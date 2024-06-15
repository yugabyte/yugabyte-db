// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.Random;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import javax.sql.DataSource;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SimpleSqlPayload {
  private static final String TABLE_NAME = "sql_payload_table";
  private AtomicInteger lastId = new AtomicInteger(1);
  private int readThreads;
  private int writeThreads;
  private long timeBetweenRetries;
  private Universe universe;

  private AtomicInteger errorCount = new AtomicInteger();
  private AtomicInteger readCount = new AtomicInteger();
  private AtomicInteger writeCount = new AtomicInteger();

  private volatile boolean stopped = false;

  public SimpleSqlPayload(
      int readThreads, int writeThreads, int timeBetweenRetries, Universe universe) {
    this.readThreads = readThreads;
    this.writeThreads = writeThreads;
    this.timeBetweenRetries = timeBetweenRetries;
    this.universe = universe;
  }

  private ExecutorService executor;
  private DataSource dataSource;
  private Long startTime;

  public void init() {
    HikariConfig config = new HikariConfig();
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    String urls =
        universe.getNodesInCluster(primaryCluster.uuid).stream()
            .map(n -> n.cloudInfo.private_ip + ":" + n.ysqlServerRpcPort)
            .collect(Collectors.joining(","));
    config.setJdbcUrl("jdbc:postgresql://" + urls + "/" + Util.YUGABYTE_DB);
    config.setUsername("yugabyte");
    config.setPassword("");
    config.setConnectionTimeout(5 * 1000);
    config.setValidationTimeout(1000);
    config.setIdleTimeout(10 * 60 * 1000);
    config.setMaxLifetime(30 * 60 * 1000);
    dataSource = new HikariDataSource(config);

    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("SimplePayload-%d").build();
    executor = Executors.newCachedThreadPool(namedThreadFactory);
  }

  public void start() {
    startTime = System.currentTimeMillis();
    try {
      createTable();
    } catch (SQLException e) {
      throw new RuntimeException(e);
    }
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
