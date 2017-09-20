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
package com.yugabyte.driver.core;

import org.junit.Test;

import com.datastax.driver.core.TableMetadata;

import org.yb.cql.BaseCQLTest;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class TestPartitionMetadata extends BaseCQLTest {

  @Test
  public void testCreateDropTable() throws Exception {

    // Create a PartitionMetadata with no refresh cycle.
    PartitionMetadata metadata = new PartitionMetadata(cluster, 0);
    final int MAX_WAIT_SECONDS = 10;

    // Create test table. Verify that the PartitionMetadata gets notified of the table creation
    // and loads the metadata.
    session.execute("create table test_partition1 (k int primary key);");
    TableMetadata table = cluster
                          .getMetadata()
                          .getKeyspace(BaseCQLTest.DEFAULT_TEST_KEYSPACE)
                          .getTable("test_partition1");
    boolean found = false;
    for (int i = 0; i < MAX_WAIT_SECONDS; i++) {
      if (metadata.getHostsForKey(table, 0).size() > 0) {
        found = true;
        break;
      }
      Thread.sleep(1000);
    }
    assertTrue(found);

    // Drop test table. Verify that the PartitionMetadata gets notified of the table drop
    // and clears the the metadata.
    session.execute("Drop table test_partition1;");
    for (int i = 0; i < MAX_WAIT_SECONDS; i++) {
      if (metadata.getHostsForKey(table, 0).size() == 0) {
        found = false;
        break;
      }
      Thread.sleep(1000);
    }
    assertFalse(found);

    // Create a PartitionMetadata with 1-second refresh cycle. Verify the metadata is refreshed
    // periodically even without table creation/drop.
    metadata = new PartitionMetadata(cluster, 1);
    final int MIN_LOAD_COUNT = 5;
    for (int i = 0; i < MAX_WAIT_SECONDS; i++) {
      if (metadata.loadCount.get() >= MIN_LOAD_COUNT)
        break;
      Thread.sleep(1000);
    }
    LOG.info("PartitionMetadata load count = " + metadata.loadCount.get());
    assertTrue(metadata.loadCount.get() >= MIN_LOAD_COUNT);
  }
}
