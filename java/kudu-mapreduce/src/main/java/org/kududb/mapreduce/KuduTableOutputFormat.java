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

import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.kududb.client.*;
import org.apache.hadoop.conf.Configurable;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.io.NullWritable;
import org.apache.hadoop.mapreduce.JobContext;
import org.apache.hadoop.mapreduce.OutputCommitter;
import org.apache.hadoop.mapreduce.OutputFormat;
import org.apache.hadoop.mapreduce.RecordWriter;
import org.apache.hadoop.mapreduce.TaskAttemptContext;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

/**
 * <p>
 * Use {@link
 * KuduTableMapReduceUtil.TableOutputFormatConfigurator}
 * to correctly setup this output format, then {@link
 * KuduTableMapReduceUtil#getTableFromContext(org.apache.hadoop.mapreduce.TaskInputOutputContext)}
 * to get a KuduTable.
 * </p>
 *
 * <p>
 * Hadoop doesn't have the concept of "closing" the output format so in order to release the
 * resources we assume that once either
 * {@link #checkOutputSpecs(org.apache.hadoop.mapreduce.JobContext)}
 * or {@link TableRecordWriter#close(org.apache.hadoop.mapreduce.TaskAttemptContext)}
 * have been called that the object won't be used again and the KuduClient is shut down.
 * </p>
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public class KuduTableOutputFormat extends OutputFormat<NullWritable,Operation>
    implements Configurable {

  private static final Logger LOG = LoggerFactory.getLogger(KuduTableOutputFormat.class);

  /** Job parameter that specifies the output table. */
  static final String OUTPUT_TABLE_KEY = "kudu.mapreduce.output.table";

  /** Job parameter that specifies where the masters are */
  static final String MASTER_ADDRESSES_KEY = "kudu.mapreduce.master.addresses";

  /** Job parameter that specifies how long we wait for operations to complete */
  static final String OPERATION_TIMEOUT_MS_KEY = "kudu.mapreduce.operation.timeout.ms";

  /** Number of rows that are buffered before flushing to the tablet server */
  static final String BUFFER_ROW_COUNT_KEY = "kudu.mapreduce.buffer.row.count";

  /**
   * Job parameter that specifies which key is to be used to reach the KuduTableOutputFormat
   * belonging to the caller
   */
  static final String MULTITON_KEY = "kudu.mapreduce.multitonkey";

  /**
   * This multiton is used so that the tasks using this output format/record writer can find
   * their KuduTable without having a direct dependency on this class,
   * with the additional complexity that the output format cannot be shared between threads.
   */
  private static final ConcurrentHashMap<String, KuduTableOutputFormat> MULTITON = new
      ConcurrentHashMap<String, KuduTableOutputFormat>();

  /**
   * This counter helps indicate which task log to look at since rows that weren't applied will
   * increment this counter.
   */
  public enum Counters { ROWS_WITH_ERRORS }

  private Configuration conf = null;

  private KuduClient client;
  private KuduTable table;
  private KuduSession session;
  private long operationTimeoutMs;

  @Override
  public void setConf(Configuration entries) {
    this.conf = new Configuration(entries);

    String masterAddress = this.conf.get(MASTER_ADDRESSES_KEY);
    String tableName = this.conf.get(OUTPUT_TABLE_KEY);
    this.operationTimeoutMs = this.conf.getLong(OPERATION_TIMEOUT_MS_KEY,
        AsyncKuduClient.DEFAULT_OPERATION_TIMEOUT_MS);
    int bufferSpace = this.conf.getInt(BUFFER_ROW_COUNT_KEY, 1000);

    this.client = new KuduClient.KuduClientBuilder(masterAddress)
        .defaultOperationTimeoutMs(operationTimeoutMs)
        .build();
    try {
      this.table = client.openTable(tableName);
    } catch (Exception ex) {
      throw new RuntimeException("Could not obtain the table from the master, " +
          "is the master running and is this table created? tablename=" + tableName + " and " +
          "master address= " + masterAddress, ex);
    }
    this.session = client.newSession();
    this.session.setFlushMode(AsyncKuduSession.FlushMode.AUTO_FLUSH_BACKGROUND);
    this.session.setMutationBufferSpace(bufferSpace);
    this.session.setIgnoreAllDuplicateRows(true);
    String multitonKey = String.valueOf(Thread.currentThread().getId());
    assert(MULTITON.get(multitonKey) == null);
    MULTITON.put(multitonKey, this);
    entries.set(MULTITON_KEY, multitonKey);
  }

  private void shutdownClient() throws IOException {
    try {
      client.shutdown();
    } catch (Exception e) {
      throw new IOException(e);
    }
  }

  public static KuduTable getKuduTable(String multitonKey) {
    return MULTITON.get(multitonKey).getKuduTable();
  }

  private KuduTable getKuduTable() {
    return this.table;
  }

  @Override
  public Configuration getConf() {
    return conf;
  }

  @Override
  public RecordWriter<NullWritable, Operation> getRecordWriter(TaskAttemptContext taskAttemptContext)
      throws IOException, InterruptedException {
    return new TableRecordWriter(this.session);
  }

  @Override
  public void checkOutputSpecs(JobContext jobContext) throws IOException, InterruptedException {
    shutdownClient();
  }

  @Override
  public OutputCommitter getOutputCommitter(TaskAttemptContext taskAttemptContext) throws
      IOException, InterruptedException {
    return new KuduTableOutputCommitter();
  }

  protected class TableRecordWriter extends RecordWriter<NullWritable, Operation> {

    private final AtomicLong rowsWithErrors = new AtomicLong();
    private final KuduSession session;

    public TableRecordWriter(KuduSession session) {
      this.session = session;
    }

    @Override
    public void write(NullWritable key, Operation operation)
        throws IOException, InterruptedException {
      try {
        session.apply(operation);
      } catch (Exception e) {
        throw new IOException("Encountered an error while writing", e);
      }
    }

    @Override
    public void close(TaskAttemptContext taskAttemptContext) throws IOException,
        InterruptedException {
      try {
        processRowErrors(session.close());
        shutdownClient();
      } catch (Exception e) {
        throw new IOException("Encountered an error while closing this task", e);
      } finally {
        if (taskAttemptContext != null) {
          // This is the only place where we have access to the context in the record writer,
          // so set the counter here.
          taskAttemptContext.getCounter(Counters.ROWS_WITH_ERRORS).setValue(rowsWithErrors.get());
        }
      }
    }

    private void processRowErrors(List<OperationResponse> responses) {
      List<RowError> errors = OperationResponse.collectErrors(responses);
      if (!errors.isEmpty()) {
        int rowErrorsCount = errors.size();
        rowsWithErrors.addAndGet(rowErrorsCount);
        LOG.warn("Got per errors for " + rowErrorsCount + " rows, " +
            "the first one being " + errors.get(0).getStatus());
      }
    }
  }
}
