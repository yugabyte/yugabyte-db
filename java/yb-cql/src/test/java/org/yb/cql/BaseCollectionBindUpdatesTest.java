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
package org.yb.cql;

import java.util.HashMap;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.Row;

import static org.yb.AssertionWrappers.assertEquals;

/**
 * Bound UPDATE of subscripted collection columns, run once per setting of
 * ycql_bind_collection_assignment_using_column_name. The flag is read only on that path, so a
 * subclass that flips it needs this one scenario and none of TestBindVariable.
 */
public abstract class BaseCollectionBindUpdatesTest extends BaseCQLTest {
  protected static enum BindCollAssignmentByColName { ON, OFF };

  protected void testCollectionBindUpdates(BindCollAssignmentByColName useColName)
      throws Exception {
    session.execute(
        "CREATE TABLE test_tbl (h int PRIMARY KEY, m map<int, varchar>, ll list<varchar>);");

    // Insert 10 rows
    for (int h = 1; h <= 10; h++) {
      session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : ?}, [?, ?, ?, ?, ?]);", h,
          h * 100, String.format("map_value_%d", h), "list_value_0", "list_value_1", "list_value_2",
          "list_value_3", "list_value_4");
    }

    // Binding only value
    {
      String bindByPosition = "UPDATE test_tbl set m[100] = ?, ll[1] = ? WHERE h = ?";
      String bindByName = "UPDATE test_tbl set m[200] = ?, ll[2] = ? WHERE h = ?";
      String bindByNamedMarkers = "UPDATE test_tbl set m[300] = :mv, ll[3] = :lv WHERE h = :pk";

      // Bind by position
      session.execute(bindByPosition, "map_updated_value_1", "list_updated_value_1", 1);
      assertQuery("SELECT * from test_tbl where h = 1",
          "Row[1, {100=map_updated_value_1}, [list_value_0, list_updated_value_1, list_value_2, "
              + "list_value_3, list_value_4]]");

      // Direct binding
      {
        // Bind by position
        session.execute(bindByPosition, "map_updated_value_1", "list_updated_value_1", 1);
        assertQuery("SELECT * from test_tbl where h = 1",
            "Row[1, {100=map_updated_value_1}, [list_value_0, list_updated_value_1, list_value_2,"
                + " list_value_3, list_value_4]]");

        // Bind by name
        {
          HashMap values = new HashMap<String, Object>() {{
              put("h", 2);
              put("value(m)", "map_updated_value_2");
              put("value(ll)", "list_updated_value_2");
            }};
          session.execute(bindByName, values);
          assertQuery("SELECT * from test_tbl where h = 2",
              "Row[2, {200=map_updated_value_2}, [list_value_0, list_value_1, "
                  + "list_updated_value_2, list_value_3, list_value_4]]");

          if (useColName == BindCollAssignmentByColName.ON) {
            // Assert backwards compatibility for syntax "UPDATE .. SET m[100] = ?, ll[2] = ? ..".
            // We also support using the column name i.e. "m" and "ll" when the GFlag
            // ycql_bind_collection_assignment_using_column_name is true.
            HashMap valuesForBackwardsCompatibility = new HashMap<String, Object>() {{
                put("h", 4);
                put("m", "map_updated_value_backwards");
                put("ll", "list_updated_value_backwards");
              }};
            session.execute(bindByName, valuesForBackwardsCompatibility);
            assertQuery("SELECT * from test_tbl where h = 4",
                "Row[4, {200=map_updated_value_backwards, 400=map_value_4}, [list_value_0, "
                    + "list_value_1, list_updated_value_backwards, list_value_3, list_value_4]]");
          }
        }

        // Bind by named markers
        {
          HashMap values = new HashMap<String, Object>() {{
              put("mv", "map_updated_value_3");
              put("lv", "list_updated_value_3");
              put("pk", 3);
            }};
          session.execute(bindByNamedMarkers, values);
          assertQuery("SELECT * from test_tbl where h = 3",
              "Row[3, {300=map_updated_value_3}, [list_value_0, list_value_1, list_value_2, "
                  + "list_updated_value_3, list_value_4]]");
        }
      }

      session.execute("truncate table test_tbl;");
      // Insert 10 rows
      for (int h = 1; h <= 10; h++) {
        session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : 'b'}, [?, ?, ?, ?, ?]);",
            h, h * 100, "list_value_0", "list_value_1", "list_value_2", "list_value_3",
            "list_value_4");
      }

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          session.execute(prepared.bind("map_updated_value_1", "list_updated_value_1", 1));
          assertQuery("SELECT * from test_tbl where h = 1",
              "Row[1, {100=map_updated_value_1}, [list_value_0, list_updated_value_1, list_value_2,"
                  + " list_value_3, list_value_4]]");
        }

        // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          // We can only support one kind of bindvar name for PreparedStatement. When the flag
          // ycql_bind_collection_assignment_using_column_name is on, we allow using the column
          // name.
          String mvName =
              useColName == BindCollAssignmentByColName.ON ? "m" : "value(m)";
          String lvName =
              useColName == BindCollAssignmentByColName.ON ? "ll" : "value(ll)";
          session.execute(prepared.bind()
                              .setInt("h", 2)
                              .setString(mvName, "map_updated_value_2")
                              .setString(lvName, "list_updated_value_2"));
          assertQuery("SELECT * from test_tbl where h = 2",
              "Row[2, {200=map_updated_value_2}, [list_value_0, list_value_1, "
                  + "list_updated_value_2, list_value_3, list_value_4]]");
        }

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          session.execute(prepared.bind().setInt("pk", 3)
                                         .setString("mv", "map_updated_value_3")
                                         .setString("lv", "list_updated_value_3"));
          assertQuery("SELECT * from test_tbl where h = 3",
              "Row[3, {300=map_updated_value_3}, [list_value_0, list_value_1, list_value_2, "
                  + "list_updated_value_3, list_value_4]]");
        }
      }
    }

    session.execute("TRUNCATE TABLE test_tbl;");
    for (int h = 1; h <= 10; h++) {
      session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : ?}, [?, ?, ?, ?, ?]);", h,
          h * 100, String.format("map_value_%d", h), "list_value_0", "list_value_1", "list_value_2",
          "list_value_3", "list_value_4");
    }

    // Binding only key
    {
      String bindByPosition = "UPDATE test_tbl set m[?] = 'map_updated_value_1', ll[?] = "
          + "'list_updated_value_1' WHERE h = ?";
      String bindByName = "UPDATE test_tbl set m[?] = 'map_updated_value_2', ll[?] = "
          + "'list_updated_value_2' WHERE h = ?";
      String bindByNamedMarkers = "UPDATE test_tbl set m[:mk] = 'map_updated_value_3', ll[:li] = "
          + "'list_updated_value_3' WHERE h = :pk";

      // Direct binding
      {
        // Bind by position
        session.execute(bindByPosition, 100, 1, 1);
        assertQuery("SELECT * from test_tbl where h = 1",
            "Row[1, {100=map_updated_value_1}, [list_value_0, list_updated_value_1, "
                + "list_value_2, list_value_3, list_value_4]]");

        // Bind by name
        {
          HashMap values = new HashMap<String, Object>() {{
              put("h", 2);
              put("key(m)", 200);
              put("idx(ll)", 2);
            }};
          session.execute(bindByName, values);
          assertQuery("SELECT * from test_tbl where h = 2",
              "Row[2, {200=map_updated_value_2}, [list_value_0, list_value_1, "
                  + "list_updated_value_2, list_value_3, list_value_4]]");
        }

        // Bind by named markers
        {
          HashMap values = new HashMap<String, Object>() {{
              put("mk", 300);
              put("li", 3);
              put("pk", 3);
            }};
          session.execute(bindByNamedMarkers, values);
          assertQuery("SELECT * from test_tbl where h = 3",
              "Row[3, {300=map_updated_value_3}, [list_value_0, list_value_1, list_value_2, "
                  + "list_updated_value_3, list_value_4]]");
        }
      }

      session.execute("truncate table test_tbl;");
      for (int h = 1; h <= 10; h++) {
        session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : 'b'}, [?, ?, ?, ?, ?]);",
            h, h * 100, "list_value_0", "list_value_1", "list_value_2", "list_value_3",
            "list_value_4");
      }

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          session.execute(prepared.bind(100, 1, 1));
          assertQuery("SELECT * from test_tbl where h = 1",
              "Row[1, {100=map_updated_value_1}, [list_value_0, list_updated_value_1, "
                  + "list_value_2, list_value_3, list_value_4]]");
        }

        // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          session.execute(
              prepared.bind().setInt("h", 2).setInt("key(m)", 200).setInt("idx(ll)", 2));
          assertQuery("SELECT * from test_tbl where h = 2",
              "Row[2, {200=map_updated_value_2}, [list_value_0, list_value_1, "
                  + "list_updated_value_2, list_value_3, list_value_4]]");
        }

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          session.execute(prepared.bind().setInt("pk", 3).setInt("mk", 300).setInt("li", 3));
          assertQuery("SELECT * from test_tbl where h = 3",
              "Row[3, {300=map_updated_value_3}, [list_value_0, list_value_1, list_value_2, "
                  + "list_updated_value_3, list_value_4]]");
        }
      }
    }

    session.execute("TRUNCATE TABLE test_tbl;");
    for (int h = 1; h <= 10; h++) {
      session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : ?}, [?, ?, ?, ?, ?]);", h,
          h * 100, String.format("map_value_%d", h), "list_value_0", "list_value_1", "list_value_2",
          "list_value_3", "list_value_4");
    }

    // Binding both key and value
    {
      String bindByPosition = "UPDATE test_tbl set m[?] = ?, ll[?] = ? WHERE h = ?";
      String bindByName = "UPDATE test_tbl set m[?] = ?, ll[?] = ? WHERE h = ?";
      String bindByNamedMarkers = "UPDATE test_tbl set m[:mk] = :mv, ll[:li] = :lv WHERE h = :pk";

      // Direct binding
      {
        // Bind by position
        session.execute(bindByPosition, 100, "map_updated_value_1", 1, "list_updated_value_1", 1);
        assertQuery("SELECT * from test_tbl where h = 1",
            "Row[1, {100=map_updated_value_1}, [list_value_0, list_updated_value_1, "
                + "list_value_2, list_value_3, list_value_4]]");

        // Bind by name
        {
          HashMap values = new HashMap<String, Object>() {{
              put("h", 2);
              put("key(m)", 200);
              put("value(m)", "map_updated_value_2");
              put("idx(ll)", 2);
              put("value(ll)", "list_updated_value_2");
            }};
          session.execute(bindByName, values);
          assertQuery("SELECT * from test_tbl where h = 2",
              "Row[2, {200=map_updated_value_2}, [list_value_0, list_value_1, "
                  + "list_updated_value_2, list_value_3, list_value_4]]");
        }

        // Bind by named markers
        {
          HashMap values = new HashMap<String, Object>() {{
              put("mk", 300);
              put("li", 3);
              put("pk", 3);
              put("mv", "map_updated_value_3");
              put("lv", "list_updated_value_3");
            }};
          session.execute(bindByNamedMarkers, values);
          assertQuery("SELECT * from test_tbl where h = 3",
              "Row[3, {300=map_updated_value_3}, [list_value_0, list_value_1, list_value_2, "
                  + "list_updated_value_3, list_value_4]]");
        }
      }

      session.execute("truncate table test_tbl;");
      for (int h = 1; h <= 10; h++) {
        session.execute("INSERT INTO test_tbl (h, m, ll) VALUES (?, {? : 'b'}, [?, ?, ?, ?, ?]);",
            h, h * 100, "list_value_0", "list_value_1", "list_value_2", "list_value_3",
            "list_value_4");
      }

      // Prepared statements
      {
        // Bind by position
        {
          PreparedStatement prepared = session.prepare(bindByPosition);
          session.execute(prepared.bind(100, "map_updated_value_1", 1, "list_updated_value_1", 1));
          assertQuery("SELECT * from test_tbl where h = 1",
              "Row[1, {100=map_updated_value_1}, [list_value_0, list_updated_value_1, "
                  + "list_value_2, list_value_3, list_value_4]]");
        }

        // Bind by name
        {
          PreparedStatement prepared = session.prepare(bindByName);
          session.execute(prepared.bind().setInt("h", 2)
                                         .setInt("key(m)", 200)
                                         .setInt("idx(ll)", 2)
                                         .setString("value(m)", "map_updated_value_2")
                                         .setString("value(ll)", "list_updated_value_2"));
          assertQuery("SELECT * from test_tbl where h = 2",
              "Row[2, {200=map_updated_value_2}, [list_value_0, list_value_1, list_updated_value_2,"
                  + " list_value_3, list_value_4]]");
        }

        // Bind by named markers
        {
          PreparedStatement prepared = session.prepare(bindByNamedMarkers);
          session.execute(prepared.bind().setInt("pk", 3)
                                         .setInt("mk", 300)
                                         .setInt("li", 3)
                                         .setString("mv", "map_updated_value_3")
                                         .setString("lv", "list_updated_value_3"));
          assertQuery("SELECT * from test_tbl where h = 3",
              "Row[3, {300=map_updated_value_3}, [list_value_0, list_value_1, list_value_2, "
                  + "list_updated_value_3, list_value_4]]");
        }
      }
    }
  }
}
