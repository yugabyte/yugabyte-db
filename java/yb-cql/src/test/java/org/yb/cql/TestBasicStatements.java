// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import org.yb.cql.TestBase;

import org.junit.Test;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.exceptions.SyntaxError;

public class TestBasicStatements extends TestBase {
  @Test
  public void testCreateTable() throws Exception {
    LOG.info("Create table ...");
    session.execute("CREATE TABLE human_resource1(id int, name varchar);");
  }

  // We need to work on reporting error from SQL before activating this test.
  /*
  @Test
  public void testInvalidStatement() throws SyntaxError {
    LOG.info("Execute nothing ...");
    thrown.expect(com.datastax.driver.core.exceptions.SyntaxError.class);
    thrown.expectMessage("unknown statement");
    session.execute("NOTHING");
  }
  */
}
