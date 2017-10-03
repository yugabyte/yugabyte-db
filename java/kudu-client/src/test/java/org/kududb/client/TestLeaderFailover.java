// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
package org.kududb.client;

import org.junit.BeforeClass;
import org.junit.Test;

import static org.junit.Assert.*;

public class TestLeaderFailover extends BaseKuduTest {

  private static final String TABLE_NAME =
      TestLeaderFailover.class.getName() + "-" + System.currentTimeMillis();
  private static KuduTable table;

  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    BaseKuduTest.setUpBeforeClass();

    CreateTableOptions builder = new CreateTableOptions().setNumReplicas(3);
    createTable(TABLE_NAME, basicSchema, builder);

    table = openTable(TABLE_NAME);
  }

  /**
   * This test writes 3 rows, kills the leader, then tries to write another 3 rows. Finally it
   * counts to make sure we have 6 of them.
   *
   * This test won't run if we didn't start the cluster.
   */
  @Test(timeout = 100000)
  public void testFailover() throws Exception {
    KuduSession session = syncClient.newSession();
    session.setIgnoreAllDuplicateRows(true);
    for (int i = 0; i < 3; i++) {
      session.apply(createBasicSchemaInsert(table, i));
    }

    // Make sure the rows are in there before messing things up.
    AsyncKuduScanner scanner = client.newScannerBuilder(table).build();
    assertEquals(3, countRowsInScan(scanner));

    killTabletLeader(table);

    for (int i = 3; i < 6; i++) {
      OperationResponse resp = session.apply(createBasicSchemaInsert(table, i));
      if (resp.hasRowError()) {
        fail("Encountered a row error " + resp.getRowError());
      }
    }

    scanner = client.newScannerBuilder(table).build();
    assertEquals(6, countRowsInScan(scanner));
  }
}