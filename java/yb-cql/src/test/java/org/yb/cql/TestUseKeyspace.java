// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import org.junit.Test;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

public class TestUseKeyspace extends BaseCQLTest {

  private void testCreateExistingKeyspace(String keyspace) throws Exception {
    try {
      createKeyspace(keyspace);
      fail("CREATE KEYSPACE \"" + keyspace + "\" did not fail");
    } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
      LOG.info("Expected InvalidQuery exception", e);
    }
  }

  private void testUseKeyspace(String keyspace, boolean create) throws Exception {
    if (create) {
      createKeyspace(keyspace);
    }
    useKeyspace(keyspace);
    assertEquals(keyspace, session.getLoggedKeyspace());
  }

  private void testUseInvalidKeyspace(String keyspace) throws Exception {
    try {
      useKeyspace(keyspace);
      fail("USE \"" + keyspace + "\" did not fail");
    } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
      LOG.info("Expected InvalidQuery exception", e);
    }
  }

  private void testUseProhibitedKeyspace(String keyspace) throws Exception {
    try {
      useKeyspace(keyspace);
      fail("USE \"" + keyspace + "\" did not fail");
    } catch (com.datastax.driver.core.exceptions.ServerError e) {
      LOG.info("Expected ServerError exception", e);
    }
  }

  @Test
  public void testUseKeyspace() throws Exception {
    LOG.info("Begin test testUseKeyspace()");

    // Using of existing default keyspace is prohibited now.
    testUseProhibitedKeyspace(DEFAULT_KEYSPACE);

    // Use existing system keyspace.
    testUseKeyspace("system", false);

    // Use existing system_schema keyspace.
    testUseKeyspace("system_schema", false);

    // Create new keyspace and use it.
    testUseKeyspace("test_keyspace", true);

    // Use a non-existent keyspace.
    testUseInvalidKeyspace("no_such_keyspace");

    LOG.info("End test testUseKeyspace()");
  }

  @Test
  public void testKeyspaceDoubleCreation() throws Exception {
    LOG.info("Begin test testKeyspaceDoubleCreation()");

    // Create new keyspace and use it.
    testUseKeyspace("new_keyspace", true);
    // Try to create already existing keyspace.
    testCreateExistingKeyspace("new_keyspace");

    // Try to create already existing default test keyspace.
    testCreateExistingKeyspace(DEFAULT_TEST_KEYSPACE);

    LOG.info("End test testKeyspaceDoubleCreation()");
  }
}
