// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.pgsql;

import com.mongodb.ConnectionString;
import com.mongodb.MongoClientSettings;
import com.mongodb.client.MongoClient;
import com.mongodb.client.MongoClients;

import java.sql.Statement;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;

import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;
import java.security.cert.X509Certificate;

import org.bson.Document;
import org.junit.After;
import org.junit.Before;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;

/**
 * Base class for DocumentDB Gateway tests. Handles cluster setup with documentdb flags,
 * extension creation, gateway worker restart, SSL context, and MongoClient lifecycle.
 *
 * Subclasses can override {@link #GATEWAY_PORT}, {@link #isAuthEnabled()},
 * and {@link #getAdditionalTServerFlags()} to customize behavior.
 */
public abstract class BaseDocumentDBGatewayTest extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(BaseDocumentDBGatewayTest.class);

  protected static final String TEST_DB = "testdb";
  protected static final int GATEWAY_PORT = 27017;

  protected MongoClient mongoClient;

  private static boolean gatewayInitialized = false;

  /** Additional tserver flags beyond the common documentdb ones. */
  protected Map<String, String> getAdditionalTServerFlags() {
    return new HashMap<>();
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("enable_pg_cron", "true");
    flagMap.put("allowed_preview_flags_csv", "ysql_enable_documentdb");
    flagMap.put("ysql_enable_documentdb", "true");
    return flagMap;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("enable_pg_cron", "true");
    flagMap.put("allowed_preview_flags_csv", "ysql_enable_documentdb");
    flagMap.put("ysql_enable_documentdb", "true");
    flagMap.put("ysql_suppress_unsafe_alter_notice", "true");
    flagMap.putAll(getAdditionalTServerFlags());
    return flagMap;
  }

  @Before
  public void setUp() throws Exception {
    String host = getPgHost(0);

    if (!gatewayInitialized) {
      // Create the documentdb extension via YSQL.
      try (Statement statement = connection.createStatement()) {
        statement.execute("CREATE EXTENSION IF NOT EXISTS documentdb CASCADE");
      }

      // TODO(#31353) The gateway background worker starts with PostgreSQL but cannot serve requests
      // until the extension schemas exist. Restart the cluster so the gateway initializes
      // with the schemas already in place.
      restartForGateway();

      // Wait for the gateway to start accepting connections (first tserver).
      waitForGateway(host);

      gatewayInitialized = true;
    }

    // mongoClient is recreated per test (tearDown closes and nulls it).
    mongoClient = createMongoClient(host);
  }

  @After
  public void tearDown() throws Exception {
    if (mongoClient != null) {
      mongoClient.close();
      mongoClient = null;
    }
  }

  /** Builds a MongoDB connection string with TLS and SCRAM-SHA-256 auth. */
  protected String buildConnectionString(String host, String extraParams) {
    int port = GATEWAY_PORT;
    String base = String.format("mongodb://%s:%s@%s:%d/?tls=true&authMechanism=SCRAM-SHA-256",
        DEFAULT_PG_USER, DEFAULT_PG_PASS, host, port);
    if (extraParams != null && !extraParams.isEmpty()) {
      base += "&" + extraParams;
    }
    return base;
  }

  protected String buildConnectionString(String host) {
    return buildConnectionString(host, null);
  }

  /** Creates a MongoClient with TLS and optional SCRAM-SHA-256 auth. */
  protected MongoClient createMongoClient(String host) throws Exception {
    SSLContext sslContext = createTrustAllSSLContext();

    MongoClientSettings settings = MongoClientSettings.builder()
        .applyConnectionString(new ConnectionString(buildConnectionString(host)))
        .applyToSslSettings(builder -> {
          builder.enabled(true);
          builder.context(sslContext);
          builder.invalidHostNameAllowed(true);
        })
        .applyToClusterSettings(builder ->
            builder.serverSelectionTimeout(30, TimeUnit.SECONDS))
        .build();

    return MongoClients.create(settings);
  }

  protected static SSLContext createTrustAllSSLContext() throws Exception {
    SSLContext ctx = SSLContext.getInstance("TLS");
    ctx.init(null, new TrustManager[]{new X509TrustManager() {
      public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }
      public void checkClientTrusted(X509Certificate[] c, String a) {}
      public void checkServerTrusted(X509Certificate[] c, String a) {}
    }}, new java.security.SecureRandom());
    return ctx;
  }

  /**
   * Restart the cluster so the gateway background workers pick up the newly created extension.
   * The gateway starts with PostgreSQL before CREATE EXTENSION runs and caches the missing
   * schema state, so a clean restart is required. This is a known limitation tracked by
   * yugabyte/yugabyte-db#31353; once the gateway can pick up the extension without a restart,
   * this helper and its callers should be removed.
   */
  protected void restartForGateway() throws Exception {
    LOG.info("Restarting cluster so gateway workers pick up the documentdb extension");
    miniCluster.restart();
    connection = getConnectionBuilder().connect();
    LOG.info("Cluster restarted");
  }

  protected void waitForGateway(String host) throws Exception {
    LOG.info("Waiting for DocumentDB gateway to be ready on {}:{}", host, GATEWAY_PORT);
    TestUtils.waitFor(() -> {
      try {
        SSLContext ctx = createTrustAllSSLContext();

        String connStr = buildConnectionString(host,
            "serverSelectionTimeoutMS=2000&connectTimeoutMS=2000");

        MongoClientSettings s = MongoClientSettings.builder()
            .applyConnectionString(new ConnectionString(connStr))
            .applyToSslSettings(b -> {
              b.enabled(true);
              b.context(ctx);
              b.invalidHostNameAllowed(true);
            })
            .build();

        try (MongoClient client = MongoClients.create(s)) {
          client.getDatabase("admin").runCommand(new Document("ping", 1));
          return true;
        }
      } catch (Exception e) {
        LOG.debug("Gateway not ready yet: {}", e.getMessage());
        return false;
      }
    }, 120000);
    LOG.info("DocumentDB gateway is ready");
  }
}
