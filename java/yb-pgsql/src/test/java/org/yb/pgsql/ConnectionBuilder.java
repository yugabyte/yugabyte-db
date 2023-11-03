// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.pgsql;

import java.net.InetSocketAddress;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;
import java.util.Map;
import java.util.Properties;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBCluster;
import org.yb.util.EnvAndSysPropertyUtil;

import com.yugabyte.PGProperty;

import static com.google.common.base.Preconditions.checkNotNull;

public class ConnectionBuilder implements Cloneable {
  private static final Logger LOG = LoggerFactory.getLogger(BasePgSQLTest.class);
  private static final int MAX_CONNECTION_ATTEMPTS = 15;
  private static final int INITIAL_CONNECTION_DELAY_MS = 500;

  // TODO(janand) GH #17899 Deduplicate DEFAULT_PG_DATABASE and TEST_PG_USER (present in
  // BasePgSQLTest and ConnectionBuilder)
  // NOTE: Values for DEFAULT_PG_DATABASE and TEST_PG_USER should be same in both
  // org.yb.pgsql.ConnectionBuilder and org.yb.pgsql.BasePgSQLTest
  public static final String DEFAULT_PG_DATABASE = "yugabyte";
  public static final String TEST_PG_USER = "yugabyte_test";

  private static final String EX_DB_STARTING = "the database system is starting up";
  private static final String EX_CONN_REFUSED =
      "refused. Check that the hostname and port are correct and that the postmaster is accepting";
  private static final String EX_DB_RECOVERY_MODE = "the database system is in recovery mode";
  private static final String EX_UNABLE_TO_CONNECT_TO_SERVER =
      "failed to connect to remote server";

  private final MiniYBCluster miniCluster;

  private boolean loadBalance;
  private int tserverIndex = 0;
  private String database = DEFAULT_PG_DATABASE;
  private String user = TEST_PG_USER;
  private String password = null;
  private String preferQueryMode = null;
  private String sslmode = null;
  private String sslcert = null;
  private String sslkey = null;
  private String sslrootcert = null;
  private IsolationLevel isolationLevel = IsolationLevel.DEFAULT;
  private AutoCommit autoCommit = AutoCommit.DEFAULT;
  private ConnectionEndpoint connectionEndpoint = ConnectionEndpoint.DEFAULT;

  public ConnectionBuilder(MiniYBCluster miniCluster) {
    this.miniCluster = checkNotNull(miniCluster);
  }

  public ConnectionBuilder withTServer(int tserverIndex) {
    ConnectionBuilder copy = clone();
    copy.tserverIndex = tserverIndex;
    return copy;
  }

  public ConnectionBuilder withDatabase(String database) {
    ConnectionBuilder copy = clone();
    copy.database = database;
    return copy;
  }

  public ConnectionBuilder withUser(String user) {
    ConnectionBuilder copy = clone();
    copy.user = user;
    return copy;
  }

  public ConnectionBuilder withPassword(String password) {
    ConnectionBuilder copy = clone();
    copy.password = password;
    return copy;
  }

  public ConnectionBuilder withIsolationLevel(IsolationLevel isolationLevel) {
    ConnectionBuilder copy = clone();
    copy.isolationLevel = isolationLevel;
    return copy;
  }

  public ConnectionBuilder withAutoCommit(AutoCommit autoCommit) {
    ConnectionBuilder copy = clone();
    copy.autoCommit = autoCommit;
    return copy;
  }

  public ConnectionBuilder withPreferQueryMode(String preferQueryMode) {
    ConnectionBuilder copy = clone();
    copy.preferQueryMode = preferQueryMode;
    return copy;
  }

  public ConnectionBuilder withSslMode(String sslmode) {
    ConnectionBuilder copy = clone();
    copy.sslmode = sslmode;
    return copy;
  }

  public ConnectionBuilder withSslCert(String sslcert) {
    ConnectionBuilder copy = clone();
    copy.sslcert = sslcert;
    return copy;
  }

  public ConnectionBuilder withSslKey(String sslkey) {
    ConnectionBuilder copy = clone();
    copy.sslkey = sslkey;
    return copy;
  }

  public ConnectionBuilder withSslRootCert(String sslrootcert) {
    ConnectionBuilder copy = clone();
    copy.sslrootcert = sslrootcert;
    return copy;
  }

  public ConnectionBuilder withConnectionEndpoint(ConnectionEndpoint connectionEndpoint) {
    ConnectionBuilder copy = clone();
    copy.connectionEndpoint = connectionEndpoint;
    return copy;
  }

  @Override
  protected ConnectionBuilder clone() {
    try {
      return (ConnectionBuilder) super.clone();
    } catch (CloneNotSupportedException ex) {
      throw new RuntimeException("This can't happen, but to keep compiler happy", ex);
    }
  }

  public Connection replicationConnect() throws Exception {
    Properties props = new Properties();
    // Ask the driver to assume that the PG version is 11.2 which is what YSQL is based on. Any
    // value above 9.4.0 works here since the support for replication connection was added to PG
    // in 9.4.0.
    PGProperty.ASSUME_MIN_SERVER_VERSION.set(props, "110200");
    PGProperty.REPLICATION.set(props, "database");

    if (preferQueryMode != null && preferQueryMode != "simple") {
      throw new RuntimeException("replication connection only supports simple query mode");
    }
    // https://github.com/pgjdbc/pgjdbc/issues/759
    PGProperty.PREFER_QUERY_MODE.set(props, "simple");
    return connect(props);
  }

  public Connection connect() throws Exception {
    return connect(new Properties());
  }

  public Connection connect(Properties additionalProperties) throws Exception {
    final InetSocketAddress postgresAddress = connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR
        ? miniCluster.getYsqlConnMgrContactPoints().get(tserverIndex)
        : miniCluster.getPostgresContactPoints().get(tserverIndex);
    String url = String.format("jdbc:yugabytedb://%s:%d/%s", postgresAddress.getHostName(),
        postgresAddress.getPort(), database);
    Properties props = new Properties();
    props.putAll(additionalProperties);
    props.setProperty("user", user);
    if (password != null) {
      props.setProperty("password", password);
    }

    if (preferQueryMode != null) {
      props.setProperty("preferQueryMode", preferQueryMode);
    }

    if (sslmode != null) {
      props.setProperty("sslmode", sslmode);
    }

    if (sslcert != null) {
      props.setProperty("sslcert", sslcert);
    }

    if (sslkey != null) {
      props.setProperty("sslkey", sslkey);
    }

    if (sslrootcert != null) {
      props.setProperty("sslrootcert", sslrootcert);
    }

    if (EnvAndSysPropertyUtil.isEnvVarOrSystemPropertyTrue("YB_PG_JDBC_TRACE_LOGGING")) {
      props.setProperty("loggerLevel", "TRACE");
    }

    String lbValue = Boolean.toString(getLoadBalance());
    props.setProperty("load-balance", lbValue);
    int delayMs = INITIAL_CONNECTION_DELAY_MS;

    for (int attempt = 1;; ++attempt) {
      Connection connection = null;
      try {
        connection = checkNotNull(DriverManager.getConnection(url, props));
        if (isolationLevel != null) {
          connection.setTransactionIsolation(isolationLevel.pgIsolationLevel);
        }

        if (autoCommit != null) {
          connection.setAutoCommit(autoCommit.enabled);
        }

        return connection;
      } catch (SQLException sqlEx) {
        // Close the connection now if we opened it, instead of waiting until the end of the
        // test.
        if (connection != null) {
          try {
            connection.close();
          } catch (SQLException closingError) {
            LOG.error("Failure to close connection during failure cleanup before a retry:",
                closingError);
            LOG.error("When handling this exception when opening/setting up connection:", sqlEx);
          }
        }

        boolean retry = false;
        if (attempt < MAX_CONNECTION_ATTEMPTS) {
          if (sqlEx.getMessage().contains(EX_DB_STARTING) ||
              sqlEx.getMessage().contains(EX_CONN_REFUSED)) {
            retry = true;
            LOG.info("Postgres is still starting up, waiting for " + delayMs + " ms. "
                + "Got message: " + sqlEx.getMessage());
          } else if (sqlEx.getMessage().contains(EX_DB_RECOVERY_MODE)) {
            retry = true;
            LOG.info("Postgres is in recovery mode, waiting for " + delayMs + " ms. "
                + "Got message: " + sqlEx.getMessage());
          } else if (connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR &&
              sqlEx.getMessage().contains(EX_UNABLE_TO_CONNECT_TO_SERVER)) {
            retry = true;
            LOG.info("Ysql Connection Manager is unable to connect to the database, waiting for "
                + delayMs + " ms. ");
          }
        }

        if (retry) {
          LOG.info("Waiting for " + delayMs + "ms. ");
          Thread.sleep(delayMs);
          delayMs = Math.min(delayMs + 500, 10000);
        } else {
          LOG.error("Exception while trying to create connection (after " + attempt + " attempts): "
              + sqlEx.getMessage());
          throw sqlEx;
        }
      }
    }
  }

  public boolean getLoadBalance() {
    return loadBalance;
  }

  public void setLoadBalance(boolean lb) {
    loadBalance = lb;
  }
}
