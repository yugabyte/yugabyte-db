// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import java.util.Arrays;

import org.junit.BeforeClass;
import org.junit.Test;

public class TestAlterTable extends BaseCQLTest {
  @BeforeClass
  public static void SetUpBeforeClass() throws Exception {
    // Setting verbose level for debugging
    BaseCQLTest.tserverArgs = Arrays.asList("--v=3");
    BaseCQLTest.setUpBeforeClass();
  }

  @Test
  public void testAlterTableCommand() throws Exception {
    LOG.info("Creating table ...");
    session.execute("CREATE TABLE human_resource1(id int primary key, nomen varchar, age int);");

    LOG.info("Adding column ...");
    session.execute("ALTER TABLE human_resource1 ADD job varchar;");

    LOG.info("Dropping column ...");
    session.execute("ALTER TABLE human_resource1 DROP age;");

    LOG.info("Dropping invalid column ...");
    runInvalidStmt("ALTER TABLE human_resource1 DROP address;");

    LOG.info("Changing type ...");
    session.execute("ALTER TABLE human_resource1 ALTER nomen TYPE int;");

    LOG.info("Renaming column ...");
    session.execute("ALTER TABLE human_resource1 RENAME nomen TO name;");

    LOG.info("With property ...");
    session.execute("ALTER TABLE human_resource1 WITH ttl=\"5\";");

    LOG.info("With invalid property ...");
    runInvalidStmt("ALTER TABLE human_resource1 WITH abcd=\"5\";");

    LOG.info("Adding 2, dropping 1...");
    runInvalidStmt("ALTER TABLE human_resource1 ADD c1 int, c2 varchar DROP job;");
  }
}
