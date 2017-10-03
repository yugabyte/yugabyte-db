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

import com.stumbleupon.async.Deferred;
import org.kududb.ColumnSchema;
import org.kududb.Schema;
import org.junit.BeforeClass;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.concurrent.TimeUnit;

import static org.kududb.Type.STRING;
import static org.kududb.client.ExternalConsistencyMode.CLIENT_PROPAGATED;
import static org.kududb.util.HybridTimeUtil.*;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

/**
 * This only tests client propagation since it's the only thing that is client-specific.
 * All the work for commit wait is done and tested on the server-side.
 */
public class TestHybridTime extends BaseKuduTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestHybridTime.class);

  // Generate a unique table name
  protected static final String TABLE_NAME =
    TestHybridTime.class.getName() + "-" + System.currentTimeMillis();

  protected static Schema schema = getSchema();
  protected static KuduTable table;

  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    BaseKuduTest.setUpBeforeClass();

    // Using multiple tablets doesn't work with the current way this test works since we could
    // jump from one TS to another which changes the logical clock.
    CreateTableOptions builder = new CreateTableOptions();
    table = createTable(TABLE_NAME, schema, builder);
  }

  private static Schema getSchema() {
    ArrayList<ColumnSchema> columns = new ArrayList<ColumnSchema>(1);
    columns.add(new ColumnSchema.ColumnSchemaBuilder("key", STRING)
        .key(true)
        .build());
    return new Schema(columns);
  }

  /**
   * We write three rows. We increment the timestamp we get back from the first write
   * by some amount. The remaining writes should force an update to the server's clock and
   * only increment the logical values.
   *
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void test() throws Exception {
    AsyncKuduSession session = client.newSession();
    session.setFlushMode(AsyncKuduSession.FlushMode.AUTO_FLUSH_SYNC);
    session.setExternalConsistencyMode(CLIENT_PROPAGATED);
    long[] clockValues;
    long previousLogicalValue = 0;
    long previousPhysicalValue = 0;

    // Test timestamp propagation with single operations
    String[] keys = new String[] {"1", "2", "3"};
    for (int i = 0; i < keys.length; i++) {
      Insert insert = table.newInsert();
      PartialRow row = insert.getRow();
      row.addString(schema.getColumnByIndex(0).getName(), keys[i]);
      Deferred<OperationResponse> d = session.apply(insert);
      OperationResponse response = d.join(DEFAULT_SLEEP);
      assertTrue(response.getWriteTimestamp() != 0);
      clockValues = HTTimestampToPhysicalAndLogical(response.getWriteTimestamp());
      LOG.debug("Clock value after write[" + i + "]: " + new Date(clockValues[0] / 1000).toString()
        + " Logical value: " + clockValues[1]);
      // on the very first write we update the clock into the future
      // so that remaining writes only update logical values
      if (i == 0) {
        assertEquals(clockValues[1], 0);
        long toUpdateTs = clockValues[0] + 5000000;
        previousPhysicalValue = toUpdateTs;
        // After the first write we fake-update the clock into the future. Following writes
        // should force the servers to update their clocks to this value.
        client.updateLastPropagatedTimestamp(
          clockTimestampToHTTimestamp(toUpdateTs, TimeUnit.MICROSECONDS));
      } else {
        assertEquals(clockValues[0], previousPhysicalValue);
        assertTrue(clockValues[1] > previousLogicalValue);
        previousLogicalValue = clockValues[1];
      }
    }

    // Test timestamp propagation with Batches
    session.setFlushMode(AsyncKuduSession.FlushMode.MANUAL_FLUSH);
    keys = new String[] {"11", "22", "33"};
    for (int i = 0; i < keys.length; i++) {
      Insert insert = table.newInsert();
      PartialRow row = insert.getRow();
      row.addString(schema.getColumnByIndex(0).getName(), keys[i]);
      session.apply(insert);
      Deferred<List<OperationResponse>> d = session.flush();
      List<OperationResponse> responses = d.join(DEFAULT_SLEEP);
      assertEquals("Response was not of the expected size: " + responses.size(),
        1, responses.size());

      OperationResponse response = responses.get(0);
      assertTrue(response.getWriteTimestamp() != 0);
      clockValues = HTTimestampToPhysicalAndLogical(response.getWriteTimestamp());
      LOG.debug("Clock value after write[" + i + "]: " + new Date(clockValues[0] / 1000).toString()
        + " Logical value: " + clockValues[1]);
      assertEquals(clockValues[0], previousPhysicalValue);
      assertTrue(clockValues[1] > previousLogicalValue);
      previousLogicalValue = clockValues[1];
    }

    // Scan all rows with READ_LATEST (the default) we should get 6 rows back
    assertEquals(6, countRowsInScan(client.newScannerBuilder(table).build()));

    // Now scan at multiple instances with READ_AT_SNAPSHOT we should get different
    // counts depending on the scan timestamp.
    long snapTime = physicalAndLogicalToHTTimestamp(previousPhysicalValue, 0);
    assertEquals(1, scanAtSnapshot(snapTime));
    snapTime = physicalAndLogicalToHTTimestamp(previousPhysicalValue, 5);
    assertEquals(4, scanAtSnapshot(snapTime));
    // Our last snap time needs to one one into the future w.r.t. the last write's timestamp
    // for us to be able to get all rows, but the snap timestamp can't be bigger than the prop.
    // timestamp so we increase both.
    client.updateLastPropagatedTimestamp(client.getLastPropagatedTimestamp() + 1);
    snapTime = physicalAndLogicalToHTTimestamp(previousPhysicalValue, previousLogicalValue + 1);
    assertEquals(6, scanAtSnapshot(snapTime));
  }

  private int scanAtSnapshot(long time) throws Exception {
    AsyncKuduScanner.AsyncKuduScannerBuilder builder = client.newScannerBuilder(table)
        .snapshotTimestampRaw(time)
        .readMode(AsyncKuduScanner.ReadMode.READ_AT_SNAPSHOT);
    return countRowsInScan(builder.build());
  }
}
