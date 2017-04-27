// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import java.util.Arrays;
import java.util.Vector;

import org.junit.Test;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

public class TestUseKeyspace extends TestBase {

  private void testUseKeyspace(String keyspace, boolean create) {
    if (create) {
      session.execute("CREATE KEYSPACE \"" + keyspace + "\";");
    }
    session.execute("USE \"" + keyspace + "\";");
    assertEquals(keyspace, session.getLoggedKeyspace());
  }

  private void testUseInvalidKeyspace(String keyspace) {
    try {
      session.execute("USE \"" + keyspace + "\";");
      fail("USE \"" + keyspace + "\" did not fail");
    } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
      LOG.info("Expected exception", e);
    }
  }

  @Test
  public void testUseKeyspace() throws Exception {
    LOG.info("Begin test");

    // Use existing default keyspace.
    testUseKeyspace(DEFAULT_KEYSPACE, false);

    // Use existing system keyspace.
    testUseKeyspace("system", false);

    // Use existing system_schema keyspace.
    testUseKeyspace("system_schema", false);

    // Create new keyspace and use it.
    testUseKeyspace("test_keyspace", true);

    // Use a non-existent keyspace.
    testUseInvalidKeyspace("no_such_keyspace");

    LOG.info("End test");
  }
}
