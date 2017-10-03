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
package org.kududb.mapreduce.tools;

import org.kududb.ColumnSchema;
import org.kududb.Schema;
import org.kududb.Type;
import org.kududb.mapreduce.CommandLineParser;
import org.kududb.mapreduce.HadoopTestingUtility;
import org.kududb.client.BaseKuduTest;
import org.kududb.client.CreateTableOptions;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.util.GenericOptionsParser;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public class ITImportCsv extends BaseKuduTest {

  private static final String TABLE_NAME =
      ITImportCsv.class.getName() + "-" + System.currentTimeMillis();

  private static final HadoopTestingUtility HADOOP_UTIL = new HadoopTestingUtility();

  private static Schema schema;

  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    BaseKuduTest.setUpBeforeClass();

    ArrayList<ColumnSchema> columns = new ArrayList<ColumnSchema>(4);
    columns.add(new ColumnSchema.ColumnSchemaBuilder("key", Type.INT32)
        .key(true)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column1_i", Type.INT32)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column2_d", Type.DOUBLE)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column3_s", Type.STRING)
        .nullable(true)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("column4_b", Type.BOOL)
        .build());
    schema = new Schema(columns);

    createTable(TABLE_NAME, schema, new CreateTableOptions());
  }

  @AfterClass
  public static void tearDownAfterClass() throws Exception {
    try {
      BaseKuduTest.tearDownAfterClass();
    } finally {
      HADOOP_UTIL.cleanup();
    }
  }

  @Test
  public void test() throws Exception {
    Configuration conf = new Configuration();
    String testHome =
        HADOOP_UTIL.setupAndGetTestDir(ITImportCsv.class.getName(), conf).getAbsolutePath();

    // Create a 2 lines input file
    File data = new File(testHome, "data.csv");
    writeCsvFile(data);

    StringBuilder sb = new StringBuilder();
    for (ColumnSchema col : schema.getColumns()) {
      sb.append(col.getName());
      sb.append(",");
    }
    sb.deleteCharAt(sb.length() - 1);
    String[] args = new String[] {
        "-D" + CommandLineParser.MASTER_ADDRESSES_KEY + "=" + getMasterAddresses(),
        sb.toString(), TABLE_NAME, data.toString()};

    GenericOptionsParser parser = new GenericOptionsParser(conf, args);
    Job job = ImportCsv.createSubmittableJob(parser.getConfiguration(), parser.getRemainingArgs());
    assertTrue("Test job did not end properly", job.waitForCompletion(true));

    assertEquals(1, job.getCounters().findCounter(ImportCsv.Counters.BAD_LINES).getValue());

    assertEquals(3, countRowsInScan(
        client.newScannerBuilder(openTable(TABLE_NAME)).build()));
    // TODO: should verify the actual returned rows, not just the count!
  }

  private void writeCsvFile(File data) throws IOException {
    FileOutputStream fos = new FileOutputStream(data);
    fos.write("1\t3\t2.3\tsome string\ttrue\n".getBytes());
    fos.write("2\t5\t4.5\tsome more\tfalse\n".getBytes());
    fos.write("3\t7\twait this is not a double\tbad row\ttrue\n".getBytes());
    fos.write("4\t9\t10\ttrailing separator isn't bad mkay?\ttrue\t\n".getBytes());
    fos.close();
  }
}
