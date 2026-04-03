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

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.fail;

import com.mongodb.ConnectionString;
import com.mongodb.MongoClientSettings;
import com.mongodb.MongoSecurityException;
import com.mongodb.client.MongoClient;
import com.mongodb.client.MongoClients;
import com.mongodb.client.MongoCollection;
import com.mongodb.client.MongoDatabase;
import com.mongodb.client.result.InsertOneResult;

import java.sql.Statement;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;

import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;
import java.security.cert.X509Certificate;

import org.bson.Document;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.YBTestRunnerNonSanOrAArch64Mac;

/**
 * Tests for the DocumentDB Gateway with authentication enabled (documentdb_enable_auth=true).
 */
@RunWith(value = YBTestRunnerNonSanOrAArch64Mac.class)
public class TestDocumentDBGatewayAuth extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestDocumentDBGatewayAuth.class);

  private static final int BASE_GATEWAY_PORT = 10270;
  private static final String TEST_DB = "testdb";

  private static boolean extensionCreated = false;

  private MongoClient mongoClient;

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
    flagMap.put("documentdb_enable_auth", "true");
    return flagMap;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    List<Map<String, String>> perTServerFlags = new ArrayList<>();
    for (int i = 0; i < NUM_TABLET_SERVERS; i++) {
      Map<String, String> flags = new HashMap<>();
      flags.put("ysql_documentdb_gateway_port", String.valueOf(BASE_GATEWAY_PORT + i));
      perTServerFlags.add(flags);
    }
    builder.perTServerFlags(perTServerFlags);
  }

  @Before
  public void setUp() throws Exception {
    if (!extensionCreated) {
      try (Statement statement = connection.createStatement()) {
        statement.execute("CREATE EXTENSION IF NOT EXISTS documentdb CASCADE");
      }
      restartGatewayWorkers();
      extensionCreated = true;
    }

    String host = getPgHost(0);
    waitForGateway(host, BASE_GATEWAY_PORT);

    SSLContext sslContext = createTrustAllSSLContext();

    String connectionString = String.format(
        "mongodb://%s:%s@%s:%d/?tls=true&authMechanism=SCRAM-SHA-256",
        DEFAULT_PG_USER, DEFAULT_PG_PASS, host, BASE_GATEWAY_PORT);

    MongoClientSettings settings = MongoClientSettings.builder()
        .applyConnectionString(new ConnectionString(connectionString))
        .applyToSslSettings(builder -> {
          builder.enabled(true);
          builder.context(sslContext);
          builder.invalidHostNameAllowed(true);
        })
        .applyToClusterSettings(builder ->
            builder.serverSelectionTimeout(30, TimeUnit.SECONDS))
        .build();

    mongoClient = MongoClients.create(settings);
  }

  @After
  public void tearDown() throws Exception {
    if (mongoClient != null) {
      try {
        mongoClient.getDatabase(TEST_DB).drop();
      } catch (Exception e) {
        LOG.warn("Failed to drop test database during cleanup", e);
      }
      mongoClient.close();
      mongoClient = null;
    }
  }

  private void restartGatewayWorkers() throws Exception {
    List<Integer> pids = new ArrayList<>();
    try (Statement stmt = connection.createStatement();
         java.sql.ResultSet rs = stmt.executeQuery(
             "SELECT pid FROM pg_stat_activity"
             + " WHERE backend_type IN"
             + " ('DocumentDB Gateway Host', 'documentdb_bg_worker_leader')")) {
      while (rs.next()) {
        pids.add(rs.getInt("pid"));
      }
    }
    LOG.info("Killing DocumentDB gateway worker PIDs: {}", pids);
    for (int pid : pids) {
      Runtime.getRuntime().exec(new String[]{"kill", "-9", String.valueOf(pid)});
    }
    Thread.sleep(5000);
    connection = getConnectionBuilder().connect();
  }

  private SSLContext createTrustAllSSLContext() throws Exception {
    SSLContext ctx = SSLContext.getInstance("TLS");
    ctx.init(null, new TrustManager[]{new X509TrustManager() {
      public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }
      public void checkClientTrusted(X509Certificate[] c, String a) {}
      public void checkServerTrusted(X509Certificate[] c, String a) {}
    }}, new java.security.SecureRandom());
    return ctx;
  }

  private void waitForGateway(String host, int port) throws Exception {
    LOG.info("Waiting for DocumentDB gateway to be ready on {}:{}", host, port);
    TestUtils.waitFor(() -> {
      try {
        SSLContext ctx = createTrustAllSSLContext();

        String connStr = String.format(
            "mongodb://%s:%s@%s:%d/?tls=true&authMechanism=SCRAM-SHA-256"
                + "&serverSelectionTimeoutMS=2000&connectTimeoutMS=2000",
            DEFAULT_PG_USER, DEFAULT_PG_PASS, host, port);

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

  @Test
  public void testAuthenticatedInsertAndFind() throws Exception {
    MongoDatabase db = mongoClient.getDatabase(TEST_DB);
    MongoCollection<Document> collection = db.getCollection("authtest");

    Document doc = new Document("name", "Alice").append("age", 30);
    InsertOneResult result = collection.insertOne(doc);
    assertNotNull(result.getInsertedId());

    Document found = collection.find(new Document("name", "Alice")).first();
    assertNotNull(found);
    assertEquals("Alice", found.getString("name"));
  }

  @Test
  public void testUnauthenticatedConnectionFails() throws Exception {
    String host = getPgHost(0);
    SSLContext sslContext = createTrustAllSSLContext();

    // Connect without credentials — should fail when auth is enabled.
    String connStr = String.format(
        "mongodb://%s:%d/?tls=true&serverSelectionTimeoutMS=5000",
        host, BASE_GATEWAY_PORT);

    MongoClientSettings settings = MongoClientSettings.builder()
        .applyConnectionString(new ConnectionString(connStr))
        .applyToSslSettings(b -> {
          b.enabled(true);
          b.context(sslContext);
          b.invalidHostNameAllowed(true);
        })
        .build();

    try (MongoClient unauthClient = MongoClients.create(settings)) {
      unauthClient.getDatabase("admin").runCommand(new Document("ping", 1));
      fail("Expected authentication failure for unauthenticated connection");
    } catch (Exception e) {
      LOG.info("Unauthenticated connection correctly rejected: {}", e.getMessage());
    }
  }
}
