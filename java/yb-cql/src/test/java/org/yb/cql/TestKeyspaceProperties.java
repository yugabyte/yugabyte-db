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
package org.yb.cql;

import org.junit.Test;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestKeyspaceProperties extends BaseCQLTest {
  private String createKeyspaceStmt(String properties) {
    return String.format("CREATE KEYSPACE test_keyspace WITH %s", properties);
  }

  private void dropKeyspace() throws Exception {
    String deleteKeyspaceStmt = "DROP KEYSPACE test_keyspace";
    session.execute(deleteKeyspaceStmt);
  }

  private void runInvalidKeyspaceProperty(String property) throws Exception {
    runInvalidStmt(createKeyspaceStmt(property));
  }

  private void runValidKeyspaceProperty(String property) throws Exception {
    session.execute(createKeyspaceStmt(property));
    dropKeyspace();
  }

  private String durableWritesStmt(String value) {
    return String.format("REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3 } " +
        "AND DURABLE_WRITES = %s;", value);
  }

  @Test
  public void testDurableWrites() throws Exception {
    // Invalid cases.
    runInvalidKeyspaceProperty("DURABLE_WRITES = true");
    runInvalidKeyspaceProperty("DURABLE_WRITES = 'true'");
    runInvalidKeyspaceProperty("DURABLE_WRITES = false");
    runInvalidKeyspaceProperty("DURABLE_WRITES = 'false'");
    runInvalidKeyspaceProperty(durableWritesStmt("1"));
    runInvalidKeyspaceProperty(durableWritesStmt("1.0"));
    runInvalidKeyspaceProperty(durableWritesStmt("'a'"));
    runInvalidKeyspaceProperty(durableWritesStmt("{'a' : 1}"));
    runInvalidKeyspaceProperty(durableWritesStmt("{}"));

    // Valid cases.
    runValidKeyspaceProperty(durableWritesStmt("true"));
    runValidKeyspaceProperty(durableWritesStmt("'true'"));
    runValidKeyspaceProperty(durableWritesStmt("false"));
    runValidKeyspaceProperty(durableWritesStmt("'false'"));

    runValidKeyspaceProperty(durableWritesStmt("TRUE"));
    runValidKeyspaceProperty(durableWritesStmt("'TRUE'"));
    runValidKeyspaceProperty(durableWritesStmt("FALSE"));
    runValidKeyspaceProperty(durableWritesStmt("'FALSE'"));
  }

  @Test
  public void testReplication() throws Exception {
    // Invalid cases.
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : true }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 1}");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 1.0 }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : {} }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 'a' }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 'a', 'replication_factor' : 3 }");
    runInvalidKeyspaceProperty("REPLICATION = { 'class' : 'SimpleStrategy' }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 'a' }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 'a' }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3.0 }");
    runInvalidKeyspaceProperty(
      "REPLICATION = { 'class' : 'SimpleStrategy', " +
        "'replication_factor' : 9223372036854775808e9223372036854775808}");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3, 'dc1' : 1 }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'replication_factor' : 3 }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'd' : 1, 'replication_factor' : 3 }");
    runInvalidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'dc1' : true }");
    runInvalidKeyspaceProperty(
      "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 9223372036854775808 }");

    // Valid cases.
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : 3 }");
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'SimpleStrategy', 'replication_factor' : '3' }");
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'dc1' : 3 }");
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'dc1' : 3, 'dc2' : 3 }");
    runValidKeyspaceProperty(
        "REPLICATION = { 'class' : 'NetworkTopologyStrategy', 'dc1' : '3' }");
  }
}
