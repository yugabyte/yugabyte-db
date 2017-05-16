// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import org.junit.Ignore;
import org.junit.Test;

public class TestBasicStatements extends BaseCQLTest {
  @Test
  public void testCreateTable() throws Exception {
    LOG.info("Create table ...");
    session.execute("CREATE TABLE human_resource1(id int primary key, name varchar);");
  }

  // We need to work on reporting error from SQL before activating this test.
  @Ignore
  public void testInvalidStatement() throws Exception {
    LOG.info("Execute nothing ...");
    thrown.expect(com.datastax.driver.core.exceptions.SyntaxError.class);
    thrown.expectMessage("unknown statement");
    session.execute("NOTHING");
  }

}
