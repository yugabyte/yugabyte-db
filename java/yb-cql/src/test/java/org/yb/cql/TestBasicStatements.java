// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import static org.junit.Assert.fail;

import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.MiniYBCluster;

import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.exceptions.SyntaxError;

public class TestBasicStatements {
  private static final Logger LOG = LoggerFactory.getLogger(TestBasicStatements.class);

  private static final String NUM_MASTERS_PROP = "NUM_MASTERS";
  private static final int NUM_TABLET_SERVERS = 3;
  private static final int DEFAULT_NUM_MASTERS = 3;

  // Number of masters that will be started for this test if we're starting
  // a cluster.
  private static final int NUM_MASTERS =
      Integer.getInteger(NUM_MASTERS_PROP, DEFAULT_NUM_MASTERS);

  private static MiniYBCluster miniCluster;

  protected static final int DEFAULT_SLEEP = 50000;

  private Cluster cluster;
  private Session session;

  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    LOG.info("Setting up before class...");

    miniCluster = new MiniYBCluster.MiniYBClusterBuilder()
                  .numMasters(NUM_MASTERS)
                  .numTservers(NUM_TABLET_SERVERS)
                  .defaultTimeoutMs(DEFAULT_SLEEP)
                  .build();

    LOG.info("Waiting for tablet servers...");
    if (!miniCluster.waitForTabletServers(NUM_TABLET_SERVERS)) {
      fail("Couldn't get " + NUM_TABLET_SERVERS + " tablet servers running, aborting");
    }
  }

  @AfterClass
  public static void tearDownAfterClass() throws Exception {
    if (miniCluster != null) {
      miniCluster.shutdown();
    }
  }

  @Before
  public void setUpBefore() throws Exception {
    cluster = Cluster.builder()
              .addContactPointsWithPorts(miniCluster.getCQLContactPoints())
              .build();
    LOG.info("Connected to cluster: " + cluster.getMetadata().getClusterName());

    session = cluster.connect();
  }

  @After
  public void tearDownAfter() throws Exception {
    session.close();
    cluster.close();
  }

  @Test
  public void testCreateTable() throws Exception {
    LOG.info("Create table ...");
    // TODO(Robert): verify that the table is created properly by querying the master catalog.
    session.execute("CREATE TABLE human_resource1(id int, name varchar);");
  }

  @Rule
  public ExpectedException thrown = ExpectedException.none();

  @Test
  public void testInvalidStatement() throws SyntaxError {
    LOG.info("Execute nothing ...");
    thrown.expect(com.datastax.driver.core.exceptions.SyntaxError.class);
    thrown.expectMessage("unknown statement");
    session.execute("NOTHING");
  }
}
