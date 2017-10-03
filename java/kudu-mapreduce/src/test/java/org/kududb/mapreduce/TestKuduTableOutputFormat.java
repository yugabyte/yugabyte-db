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
package org.kududb.mapreduce;

import org.kududb.client.*;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.io.NullWritable;
import org.apache.hadoop.mapreduce.RecordWriter;
import org.junit.BeforeClass;
import org.junit.Test;

import static org.junit.Assert.*;

public class TestKuduTableOutputFormat extends BaseKuduTest {

  private static final String TABLE_NAME =
      TestKuduTableOutputFormat.class.getName() + "-" + System.currentTimeMillis();

  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    BaseKuduTest.setUpBeforeClass();
  }

  @Test
  public void test() throws Exception {
    createTable(TABLE_NAME, getBasicSchema(), new CreateTableOptions());

    KuduTableOutputFormat output = new KuduTableOutputFormat();
    Configuration conf = new Configuration();
    conf.set(KuduTableOutputFormat.MASTER_ADDRESSES_KEY, getMasterAddresses());
    conf.set(KuduTableOutputFormat.OUTPUT_TABLE_KEY, TABLE_NAME);
    output.setConf(conf);

    String multitonKey = conf.get(KuduTableOutputFormat.MULTITON_KEY);
    KuduTable table = KuduTableOutputFormat.getKuduTable(multitonKey);
    assertNotNull(table);

    Insert insert = table.newInsert();
    PartialRow row = insert.getRow();
    row.addInt(0, 1);
    row.addInt(1, 2);
    row.addInt(2, 3);
    row.addString(3, "a string");
    row.addBoolean(4, true);

    RecordWriter<NullWritable, Operation> rw = output.getRecordWriter(null);
    rw.write(NullWritable.get(), insert);
    rw.close(null);
    AsyncKuduScanner.AsyncKuduScannerBuilder builder = client.newScannerBuilder(table);
    assertEquals(1, countRowsInScan(builder.build()));
  }
}
