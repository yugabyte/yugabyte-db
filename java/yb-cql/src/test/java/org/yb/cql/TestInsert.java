// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import org.yb.cql.TestBase;

import org.junit.Test;

import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.exceptions.SyntaxError;

public class TestInsert extends TestBase {
  @Test
  public void testSimpleInsert() throws Exception {
    LOG.info("TEST SIMPLE INSERT - Start");

    // Setup table and insert 100 rows.
    SetupTable("test_insert", 100);

    LOG.info("TEST SIMPLE INSERT - End");
  }
}
