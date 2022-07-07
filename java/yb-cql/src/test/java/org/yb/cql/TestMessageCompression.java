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

import java.util.*;

import org.apache.commons.lang3.RandomStringUtils;
import org.junit.Test;

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.ProtocolOptions.Compression;

import static org.yb.AssertionWrappers.assertEquals;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestMessageCompression extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestMessageCompression.class);

  @Test
  public void testLZ4() throws Exception {

    // Test connection with LZ4 compression.
    try (Cluster c = getDefaultClusterBuilder()
                     .withCompression(Compression.LZ4)
                     .build();
         Session s = c.connect(DEFAULT_TEST_KEYSPACE)) {

      final int NUM_KEYS = 10;
      final int NUM_RANGE = 10;

      // Create table and insert some rows.
      s.execute("create table test_lz4 (h int, r int, c text, primary key ((h), r));");
      PreparedStatement stmt = s.prepare("insert into test_lz4 (h, r, c) values (?, ?, ?);");
      Set<String> expected = new HashSet<String>();
      for (int i = 1; i <= NUM_KEYS; i++) {
        for (int j = 1; j <= NUM_KEYS; j++) {
          String val = "v" + i + j;
          s.execute(stmt.bind(Integer.valueOf(i), Integer.valueOf(j), val));
          expected.add(String.format("Row[%d, %d, %s]", i, j, val));
        }
      }

      // Select and verify the rows.
      Set<String> actual = new HashSet<String>();
      for (Row r : s.execute("select * from test_lz4;")) {
        actual.add(r.toString());
      }
      assertEquals(expected, actual);

      // Insert large string, select it back and verify.
      String string = RandomStringUtils.randomAscii(8 * 1024 * 1024);
      LOG.info("string = \"" + string + "\"");
      s.execute("insert into test_lz4 (h, r, c) values (100, 100, ?);", string);
      Row row = s.execute("select c from test_lz4 where h = 100 and r = 100;").one();
      assertEquals(string, row.getString("c"));
    }
  }

  @Test
  public void testSnappy() throws Exception {

    // Test connection with Snappy compression.
    try (Cluster c = getDefaultClusterBuilder()
                     .withCompression(Compression.SNAPPY)
                     .build();
         Session s = c.connect(DEFAULT_TEST_KEYSPACE)) {

      final int NUM_KEYS = 10;
      final int NUM_RANGE = 10;

      // Create table and upsert some rows.
      BatchStatement batch = new BatchStatement();
      s.execute("create table test_snappy (h int, r int, c text, primary key ((h), r));");
      PreparedStatement stmt = s.prepare("update test_snappy set c = ? where h = ? and r = ?;");
      Set<String> expected = new HashSet<String>();
      for (int i = 1; i <= NUM_KEYS; i++) {
        for (int j = 1; j <= NUM_KEYS; j++) {
          String val = "v" + i + j;
          batch.add(stmt.bind(val, Integer.valueOf(i), Integer.valueOf(j)));
          expected.add(String.format("Row[%d, %d, %s]", i, j, val));
        }
      }
      s.execute(batch);

      // Select and verify the rows.
      Set<String> actual = new HashSet<String>();
      for (Row r : s.execute("select * from test_snappy;")) {
        actual.add(r.toString());
      }
      assertEquals(expected, actual);

      // Insert large string, select it back and verify.
      String string = RandomStringUtils.randomAscii(8 * 1024 * 1024);
      LOG.info("string = \"" + string + "\"");
      s.execute("insert into test_snappy (h, r, c) values (100, 100, ?);", string);
      Row row = s.execute("select c from test_snappy where h = 100 and r = 100;").one();
      assertEquals(string, row.getString("c"));
    }
  }
}
