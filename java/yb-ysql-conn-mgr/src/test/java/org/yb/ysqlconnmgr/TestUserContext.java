// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//


package org.yb.ysqlconnmgr;

import java.sql.*;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.pgsql.ConnectionEndpoint;
import static org.yb.AssertionWrappers.*;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestUserContext extends BaseYsqlConnMgr {
  private final TestUser[] TEST_USERS = new TestUser[] {
      new TestUser("user1", "grant all privileges on table emp1 to user1;",
          new String[] {"select * from emp1;", "insert into emp1 values(4, 'manav');"},
          new String[] {"select * from emp2;", "delete from emp2;",
              "insert into emp2 values (3,  'abc');"}),
      new TestUser("user2", "grant select, insert on table emp1 to user2;",
          new String[] {"select * from emp1;", "insert into emp1 values(5, 'rohan');"},
          new String[] {"delete from emp1;", "select * from emp2", "delete from emp2;",
              "update emp2 set name= 'abc'; "}),
      new TestUser("user3", "grant delete on table emp1 to user3;",
          new String[] {"delete from emp1;"},
          new String[] {"select * from emp1;", "insert into emp1 values (19, 'abc');",
              "update emp1 set name = 'abc';"}),
      new TestUser("user4", "grant insert, delete on table emp2 to user4;",
          new String[] {"insert into emp2 values(2, 'abc');", "delete from emp2;"},
          new String[] {"select * from emp1;", "delete from emp1;",
              "insert into emp1 values (4, 'abc')"}),
      new TestUser("user5", "grant all privileges on table emp2 to user5;",
          new String[] {"insert into emp2 values (9, 'abc');",
              "update emp2 set name = 'bcd' where id = 9;"},
          new String[] {"delete from emp1;", "select * from emp1;",
              "insert into emp1 values (19, 'abc');"}),
      new TestUser("user6", "grant update on table emp2 to user6;",
          new String[] {"update emp2 set name = 'bcd';"},
          new String[] {"delete from emp1;", "insert into emp1 values (19, 'abc');",
              "update emp2 set name = 'bcd' where id = 9;"}),
      new TestUser("user7", "grant delete on table emp3 to user7;",
          new String[] {"delete from emp3;"},
          new String[] {"select * from emp2;", "select * from emp3;", "delete from emp2;",
              "insert into emp2 values (2, 'abc');"}),
      new TestUser("user8", "grant insert on table emp3 to user8;",
          new String[] {"insert into emp3 values (19, 'def');"},
          new String[] {"delete from emp2;"}),
      new TestUser("user9", "grant all privileges on table emp3 to user9;",
          new String[] {"insert into emp3 values (19, 'def');", "select * from emp3;"},
          new String[] {"delete from emp2;", "select * from emp2;",
              "insert into emp2 values (3, 'abc');"}),
      new TestUser("user10", "grant update, select on table emp3 to user10;",
          new String[] {"update emp3 set name = 'abc' where id = 8;"},
          new String[] {"delete from emp3", "insert into emp3 values (12, 'abc');"}),
      new TestUser("user11", "grant update on table emp3 to user11;",
          new String[] {"update emp3 set name = 'abc';"},
          new String[] {"update emp3 set name = 'abc' where id = 8;", "delete from emp3",
              "insert into emp3 values (12, 'abc');"})};

  private final int NUM_USERS = TEST_USERS.length;
  private final int NUM_THREADS_PER_USER = 3;
  private final int NUM_THREADS = NUM_THREADS_PER_USER * NUM_USERS;
  private final String ROLE_NAME = "role_1";

  public void setUpUserContext() {
    try (Connection conn = getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.DEFAULT)
                                                 .withUser("yugabyte")
                                                 .connect();
         Statement stmt = conn.createStatement()) {

      stmt.executeUpdate("create table emp1 (id int, name varchar);");
      stmt.executeUpdate("create table emp2 (id int, name varchar);");
      stmt.executeUpdate("create table emp3 (id int, name varchar);");

      for (int i = 1; i < 10; i++) {
        stmt.executeUpdate("INSERT INTO EMP1 VALUES (" + i + ", 'abc');");
      }

      for (int i = 1; i < 10; i++) {
        stmt.executeUpdate("INSERT INTO EMP2 VALUES (" + i + ", 'def');");
      }

      for (int i = 1; i < 10; i++) {
        stmt.executeUpdate("INSERT INTO EMP3 VALUES (" + i + ", 'ghi');");
      }

      for (TestUser user : TEST_USERS) {
        user.createUser(user.user_name, stmt);
        user.grantPrivileges(user.user_name, stmt, user.grant_access);
      }
    } catch (Exception e) {
      LOG.error("Problem while initialization", e);
      fail();
    }
  }

  @Test
  public void testRoleCantLogin() throws Exception {
    // Create a role
    getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.DEFAULT)
                          .connect()
                          .createStatement()
                          .executeUpdate(String.format("CREATE ROLE %s;", ROLE_NAME));

    Connection conn = null;
    try {
      conn = getConnectionBuilder().withUser(ROLE_NAME)
                                   .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                                   .connect();
      fail("A role was able to connect to the database");
    } catch (Exception e) {
      LOG.debug("Got the expected error while trying to create connection with a role", e);
    } finally {
      if (conn != null && !conn.isClosed())
        conn.close();
    }
  }

  @Test
  public void testUserContext() throws Exception {
    TransactionRunnable[] runnables = new TransactionRunnable[NUM_THREADS];
    Thread[] threads = new Thread[NUM_THREADS];

    setUpUserContext();

    for (int i = 0; i < NUM_USERS; i++) {
      TestUser user = TEST_USERS[i];
      for (int j = 0; j < NUM_THREADS_PER_USER; j++) {
        int thread_id = i * NUM_THREADS_PER_USER + j;
        runnables[thread_id] = new TransactionRunnable(user);
        threads[thread_id] = new Thread(runnables[thread_id]);
      }
    }

    // Start the threads and wait for each of them to execute.
    for (Thread thread : threads) {
      thread.start();
    }

    for (Thread thread : threads) {
      thread.join();
    }

    for (TransactionRunnable runnable : runnables) {
      assertFalse(runnable.got_except);
    }
  }

  // TODO (janand): Shift it to the BaseYsqlConnMgr
  private final String[] RETRYABLE_ERROR_MESSAGES =
  {"could not serialize access due to concurrent update",
    "Catalog Version Mismatch",
    "Restart read required",
    "expired or aborted by a conflict",
    "Transaction "
  };

  private boolean isRetryable(Exception e) {
    for (String allowed_msg : RETRYABLE_ERROR_MESSAGES) {
      if (e.getMessage().contains(allowed_msg)) {
        return true;
      }
    }
    return false;
  }

  private class TransactionRunnable implements Runnable {
    private TestUser user = null;
    public Boolean got_except = false;

    public TransactionRunnable(TestUser user) {
      this.user = user;
    }

    @Override
    public void run() {
      Connection conn = null;

      try {
        conn = getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                                     .withUser(user.user_name)
                                     .connect();
        Statement stmt = conn.createStatement();
        // Run Pos TC.
        for (String sql : user.pos_tc) {
          try {
            if (sql.charAt(0) == 's' || sql.charAt(0) == 'S') {
              // Select statement
              ResultSet rs = stmt.executeQuery(sql);
            } else {
              stmt.executeUpdate(sql);
            }
          } catch (Exception e) {
            // TODO(janand) : Retry the query.
            if(isRetryable(e))
              continue;
            got_except = true;
            fail("Got unexpected error while running postive testcases " + e.getMessage());
          }
        }

        // Run Neg TC.
        for (String sql : user.neg_tc) {
          Boolean got_err = false;
          try {
            if (sql.charAt(0) == 's' || sql.charAt(0) == 'S') {
              ResultSet rs = stmt.executeQuery(sql);
            } else {
              stmt.executeUpdate(sql);
            }
          } catch (Exception e) {
            LOG.debug("Got expected error", e);
            got_err = true;
          }

          if (!got_err) {
            got_except = true;
            fail("Didn't got error with user " + user.user_name + " when exectued " + sql);
          }
        }
      } catch (Exception e) {
        got_except = true;
        LOG.error("Transaction failed while running the positive and negative test cases.", e);
        fail();
      } finally {
        try {
          if (conn != null && !conn.isClosed())
            conn.close();
        } catch (SQLException e) {
          LOG.error("Got error while closing the connection");
        }
      }
    }
  }

  private class TestUser {
    private String user_name;
    private String grant_access;
    private String[] pos_tc;
    private String[] neg_tc;

    public TestUser(String name, String grant_access, String[] pos_tc, String[] neg_tc) {
      this.user_name = name;
      this.grant_access = grant_access;
      this.pos_tc = pos_tc;
      this.neg_tc = neg_tc;
    }

    public void createUser(String user_name, Statement stmt) {
      String createUserSQL = String.format("create user %s;", user_name);

      LOG.debug("Create SQL stmt : " + createUserSQL);
      try {
        stmt.executeUpdate(createUserSQL);
        LOG.debug("Created user " + user_name);
      } catch (Exception e) {
        LOG.error("Error while creating the user " + user_name, e);
        fail();
      }
    }

    public void grantPrivileges(String user_name, Statement stmt, String prvl) {
      try {
        stmt.executeUpdate(prvl);
      } catch (Exception e) {
        LOG.error("Error while granting privileges", e);
        fail();
      }
    }
  }
}
