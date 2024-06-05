// Copyright (c) YugaByte, Inc.
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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

import java.sql.ResultSet;
import java.sql.Statement;

import static org.yb.AssertionWrappers.assertEquals;

/**
 * Tests for `COMMENT ON ...` PostgreSQL queries.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgCommentOn extends BasePgSQLTest {
  @Test
  public void testSupportedVariants() throws Exception {
    try (Statement statement = connection.createStatement()) {
      // Setup
      statement.execute("CREATE TABLE test(id serial PRIMARY KEY)");
      statement.execute("CREATE SEQUENCE some_sequence");
      statement.execute("CREATE VIEW some_view AS SELECT 'Hello World!';");
      long largeObjectId = getSystemTableRowsList(
        statement, "SELECT lo_create(0)").get(0).getLong(0);

      // Exercise
      statement.execute("COMMENT ON COLUMN test.id IS 'some column comment'");
      statement.execute("COMMENT ON INDEX test_pkey IS 'some primary key comment'");
      statement.execute("COMMENT ON SEQUENCE some_sequence IS 'some sequence comment'");
      statement.execute("COMMENT ON TABLE test IS 'some table comment'");
      statement.execute("COMMENT ON VIEW some_view IS 'some view comment'");
      statement.execute("COMMENT ON ACCESS METHOD btree IS 'some btree comment'");
      statement.execute(String.format(
          "COMMENT ON DATABASE %s IS 'default database'",
          DEFAULT_PG_DATABASE
      ));
      statement.execute("COMMENT ON SCHEMA pg_catalog IS 'some schema comment'");
      statement.execute("COMMENT ON FUNCTION pg_advisory_unlock_all IS 'unlocking function'");
      statement.execute("COMMENT ON CONSTRAINT test_pkey ON test IS 'some constraint comment'");
      statement.execute(String.format(
          "COMMENT ON LARGE OBJECT %s IS 'some large object'",
          largeObjectId
      ));
      statement.execute("COMMENT ON CAST (bit AS bigint) IS 'bit to bigint cast'");
      statement.execute("COMMENT ON TYPE bool IS 'bool type comment'");
      statement.execute("COMMENT ON OPERATOR = (text, text) IS 'text equality operator'");
      statement.execute("COMMENT ON COLLATION \"default\" IS 'some collation comment'");
      statement.execute("COMMENT ON LANGUAGE internal IS 'some internal language'");
      statement.execute("COMMENT ON ROLE yugabyte IS 'some role comment'");
      statement.execute("COMMENT ON TABLESPACE pg_default IS 'some tablespace comment'");
      statement.execute("COMMENT ON AGGREGATE max (int) IS 'some aggregate comment'");
      statement.execute("COMMENT ON RULE pg_settings_u ON pg_settings IS 'some rule comment'");

      // Verify
      assertEquals("some column comment", getObjectComment("table column", "test, id"));
      assertEquals("some primary key comment", getObjectComment("index", "test_pkey"));
      assertEquals("some sequence comment", getObjectComment("sequence", "some_sequence"));
      assertEquals("some table comment", getObjectComment("table", "test"));
      assertEquals("some view comment", getObjectComment("view", "some_view"));
      assertEquals("some btree comment", getObjectComment("access method", "btree"));
      assertEquals("default database", getSharedObjectComment("database", DEFAULT_PG_DATABASE));
      assertEquals("some schema comment", getObjectComment("schema", "pg_catalog"));
      assertEquals("unlocking function", getObjectComment("function", "pg_advisory_unlock_all"));
      assertEquals(
          "some constraint comment",
          getObjectComment("table constraint", "test, test_pkey")
      );

      String largeObjectComment = getSingleRow(statement, String.format(
          "SELECT description FROM pg_description WHERE objoid=%s", largeObjectId
      )).getString(0);
      assertEquals("some large object", largeObjectComment);

      assertEquals("bit to bigint cast", getObjectComment("cast", "bit", "bigint"));
      assertEquals("bool type comment", getObjectComment("type", "bool"));
      assertEquals(
          "text equality operator",
          getObjectComment("operator", "=", "text, text")
      );
      assertEquals("some collation comment", getObjectComment("collation", "default"));
      assertEquals("some internal language", getObjectComment("language", "internal"));
      assertEquals("some role comment", getSharedObjectComment("role", "yugabyte"));
      assertEquals("some tablespace comment", getSharedObjectComment("tablespace", "pg_default"));
      assertEquals("some aggregate comment", getObjectComment("aggregate", "max", "int"));
      assertEquals("some rule comment", getObjectComment("rule", "pg_settings, pg_settings_u"));
    }
  }

  private static String getObjectComment(String type, String name) throws Exception {
    return getObjectComment(type, name, "");
  }

  private static String getObjectComment(String type, String name, String args) throws Exception {
    try (Statement statement = connection.createStatement()) {
      String query = String.format("SELECT d.description " +
              "FROM pg_get_object_address('%s', '{%s}', '{%s}') a " +
              "JOIN pg_description d " +
              "ON d.classoid = a.classid " +
              "AND d.objoid = a.objid " +
              "AND d.objsubid = a.objsubid;",
          type,
          name,
          args
      );

      ResultSet resultSet = statement.executeQuery(query);
      resultSet.next();
      return resultSet.getString(1);
    }
  }

  private static String getSharedObjectComment(String type, String name) throws Exception {
    try (Statement statement = connection.createStatement()) {
      String query = String.format("SELECT d.description " +
              "FROM pg_get_object_address('%s', '{%s}', '{}') a " +
              "JOIN pg_shdescription d " +
              "ON d.classoid = a.classid " +
              "AND d.objoid = a.objid;",
          type,
          name
      );

      ResultSet resultSet = statement.executeQuery(query);
      resultSet.next();
      return resultSet.getString(1);
    }
  }
}
