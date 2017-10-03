/**
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. See accompanying LICENSE file.
 */
package org.kududb.mapreduce.tools;

import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableList;
import org.kududb.ColumnSchema;
import org.kududb.Schema;
import org.kududb.Type;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.kududb.client.*;
import org.kududb.mapreduce.CommandLineParser;
import org.kududb.mapreduce.KuduTableMapReduceUtil;
import org.kududb.util.Pair;
import org.apache.commons.cli.CommandLine;
import org.apache.commons.cli.GnuParser;
import org.apache.commons.cli.HelpFormatter;
import org.apache.commons.cli.Options;
import org.apache.commons.cli.ParseException;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.conf.Configured;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.io.BytesWritable;
import org.apache.hadoop.io.NullWritable;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.io.Writable;
import org.apache.hadoop.mapreduce.Counter;
import org.apache.hadoop.mapreduce.CounterGroup;
import org.apache.hadoop.mapreduce.Counters;
import org.apache.hadoop.mapreduce.InputFormat;
import org.apache.hadoop.mapreduce.InputSplit;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.mapreduce.JobContext;
import org.apache.hadoop.mapreduce.Mapper;
import org.apache.hadoop.mapreduce.RecordReader;
import org.apache.hadoop.mapreduce.Reducer;
import org.apache.hadoop.mapreduce.TaskAttemptContext;
import org.apache.hadoop.mapreduce.lib.input.FileInputFormat;
import org.apache.hadoop.mapreduce.lib.input.SequenceFileInputFormat;
import org.apache.hadoop.mapreduce.lib.output.FileOutputFormat;
import org.apache.hadoop.mapreduce.lib.output.NullOutputFormat;
import org.apache.hadoop.mapreduce.lib.output.SequenceFileOutputFormat;
import org.apache.hadoop.mapreduce.lib.output.TextOutputFormat;
import org.apache.hadoop.util.Tool;
import org.apache.hadoop.util.ToolRunner;

import java.io.DataInput;
import java.io.DataOutput;
import java.io.IOException;
import java.math.BigInteger;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Iterator;
import java.util.List;
import java.util.Random;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * <p>
 * This is an integration test borrowed from goraci, written by Keith Turner,
 * which is in turn inspired by the Accumulo test called continous ingest (ci).
 * The original source code can be found here:
 * </p>
 * <ul>
 * <li>
 * <a href="https://github.com/keith-turner/goraci">https://github.com/keith-turner/goraci</a>
 * </li>
 * <li>
 * <a href="https://github.com/enis/goraci/">https://github.com/enis/goraci/</a>
 * </li>
 * </ul>
 *
 * <p>
 * Apache Accumulo has a simple test suite that verifies that data is not
 * lost at scale. This test suite is called continuous ingest. This test runs
 * many ingest clients that continually create linked lists containing 25
 * million nodes. At some point the clients are stopped and a map reduce job is
 * run to ensure no linked list has a hole. A hole indicates data was lost.
 * </p>
 *
 * <p>
 * The nodes in the linked list are random. This causes each linked list to
 * spread across the table. Therefore if one part of a table loses data, then it
 * will be detected by references in another part of the table.
 * </p>
 *
 * <h3>
 * THE ANATOMY OF THE TEST
 * </h3>
 *
 * <p>
 * Below is rough sketch of how data is written. For specific details look at
 * the Generator code.
 * </p>
 * <ol>
 * <li>
 * Write out 1 million nodes
 * </li>
 * <li>
 * Flush the client
 * </li>
 * <li>
 * Write out 1 million that reference previous million
 * </li>
 * <li>
 * If this is the 25th set of 1 million nodes, then update 1st set of million to point to last
 * </li>
 * <li>
 * Goto 1
 * </li>
 * </ol>
 *
 * <p>
 * The key is that nodes only reference flushed nodes. Therefore a node should
 * never reference a missing node, even if the ingest client is killed at any
 * point in time.
 * </p>
 *
 * <p>
 * When running this test suite w/ Accumulo there is a script running in
 * parallel called the Agitator that randomly and continuously kills server
 * processes. The outcome was that many data loss bugs were found in Accumulo
 * by doing this. This test suite can also help find bugs that impact uptime
 * and stability when run for days or weeks.
 * </p>
 *
 * <p>
 * This test suite consists the following:
 * </p>
 * <ul>
 * <li>
 * A few Java programs
 * </li>
 * <li>
 * A little helper script to run the java programs
 * </li>
 * <li>
 * A maven script to build it.
 * </li>
 * </ul>
 *
 * <p>
 * When generating data, its best to have each map task generate a multiple of
 * 25 million. The reason for this is that circular linked list are generated
 * every 25M. Not generating a multiple in 25M will result in some nodes in the
 * linked list not having references. The loss of an unreferenced node can not
 * be detected.
 * </p>
 *
 * <h3>
 * Below is a description of the Java programs
 * </h3>
 *
 * <ul>
 * <li>
 * Generator - A map only job that generates data. As stated previously,
 * its best to generate data in multiples of 25M.
 * </li>
 * <li>
 * Verify - A map reduce job that looks for holes. Look at the counts after running. REFERENCED and
 * UNREFERENCED are ok, any UNDEFINED counts are bad. Do not run at the same
 * time as the Generator.
 * </li>
 * <li>
 * Print - A standalone program that prints nodes in the linked list
 * </li>
 * <li>
 * Delete - Disabled. A standalone program that deletes a single node
 * </li>
 * <li>
 * Walker - Disabled. A standalong program that start following a linked list and emits timing
 * info.
 * </li>
 * </ul>
 *
 * <h3>
 * KUDU-SPECIFIC CHANGES
 * </h3>
 *
 * <ul>
 * <li>
 * The 16 bytes row key is divided into two 8 byte long since we don't have a "bytes" type in
 * Kudu. Note that the C++ client can store bytes directly in string columns. Using longs
 * enables us to pretty print human readable keys than can then be passed back just as easily.
 * </li>
 * <li>
 * The table can be pre-split when running the Generator. The row keys' first component will be
 * spread over the Long.MIN_VALUE - Long.MAX_VALUE keyspace.
 * </li>
 * <li>
 * The Walker and Deleter progams were disabled to save some time but they can be re-enabled then
 * ported to Kudu without too much effort.
 * </li>
 * </ul>
 */
@InterfaceAudience.Private
@InterfaceStability.Unstable
public class IntegrationTestBigLinkedList extends Configured implements Tool {
  private static final byte[] NO_KEY = new byte[1];

  protected static final String TABLE_NAME_KEY = "IntegrationTestBigLinkedList.table";

  protected static final String DEFAULT_TABLE_NAME = "IntegrationTestBigLinkedList";

  protected static final String HEADS_TABLE_NAME_KEY = "IntegrationTestBigLinkedList.heads_table";

  protected static final String DEFAULT_HEADS_TABLE_NAME = "IntegrationTestBigLinkedListHeads";

  /** Row key, two times 8 bytes. */
  private static final String COLUMN_KEY_ONE = "key1";
  private static final String COLUMN_KEY_TWO = "key2";

  /** Link to the id of the prev node in the linked list, two times 8 bytes. */
  private static final String COLUMN_PREV_ONE = "prev1";
  private static final String COLUMN_PREV_TWO = "prev2";

  /** identifier of the mapred task that generated this row. */
  private static final String COLUMN_CLIENT = "client";

  /** the id of the row within the same client. */
  private static final String COLUMN_ROW_ID = "row_id";

  /** The number of times this row was updated. */
  private static final String COLUMN_UPDATE_COUNT = "update_count";

  /** How many rows to write per map task. This has to be a multiple of 25M. */
  private static final String GENERATOR_NUM_ROWS_PER_MAP_KEY
      = "IntegrationTestBigLinkedList.generator.num_rows";

  private static final String GENERATOR_NUM_MAPPERS_KEY
      = "IntegrationTestBigLinkedList.generator.map.tasks";

  private static final String GENERATOR_WIDTH_KEY
      = "IntegrationTestBigLinkedList.generator.width";

  private static final String GENERATOR_WRAP_KEY
      = "IntegrationTestBigLinkedList.generator.wrap";

  private static final int WIDTH_DEFAULT = 1000000;
  private static final int WRAP_DEFAULT = 25;
  private static final int ROWKEY_LENGTH = 16;

  private String toRun;
  private String[] otherArgs;

  static class CINode {
    String key;
    String prev;
    String client;
    long rowId;
    int updateCount;
  }

  static Schema getTableSchema() {
    List<ColumnSchema> columns = new ArrayList<ColumnSchema>(7);
    columns.add(new ColumnSchema.ColumnSchemaBuilder(COLUMN_KEY_ONE, Type.INT64)
        .key(true)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder(COLUMN_KEY_TWO, Type.INT64)
        .key(true)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder(COLUMN_PREV_ONE, Type.INT64)
        .nullable(true)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder(COLUMN_PREV_TWO, Type.INT64)
        .nullable(true)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder(COLUMN_ROW_ID, Type.INT64)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder(COLUMN_CLIENT, Type.STRING)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder(COLUMN_UPDATE_COUNT, Type.INT32)
        .build());
    return new Schema(columns);
  }

  static Schema getHeadsTableSchema() {
    List<ColumnSchema> columns = new ArrayList<ColumnSchema>(2);
    columns.add(new ColumnSchema.ColumnSchemaBuilder(COLUMN_KEY_ONE, Type.INT64)
        .key(true)
        .build());
    columns.add(new ColumnSchema.ColumnSchemaBuilder(COLUMN_KEY_TWO, Type.INT64)
        .key(true)
        .build());
    return new Schema(columns);
  }

  /**
   * A Map only job that generates random linked list and stores them.
   */
  static class Generator extends Configured implements Tool {

    private static final Log LOG = LogFactory.getLog(Generator.class);

    static class GeneratorInputFormat extends InputFormat<BytesWritable,NullWritable> {
      static class GeneratorInputSplit extends InputSplit implements Writable {
        @Override
        public long getLength() throws IOException, InterruptedException {
          return 1;
        }
        @Override
        public String[] getLocations() throws IOException, InterruptedException {
          return new String[0];
        }
        @Override
        public void readFields(DataInput arg0) throws IOException {
        }
        @Override
        public void write(DataOutput arg0) throws IOException {
        }
      }

      static class GeneratorRecordReader extends RecordReader<BytesWritable,NullWritable> {
        private long count;
        private long numNodes;
        private Random rand;

        @Override
        public void close() throws IOException {
        }

        @Override
        public BytesWritable getCurrentKey() throws IOException, InterruptedException {
          byte[] bytes = new byte[ROWKEY_LENGTH];
          rand.nextBytes(bytes);
          return new BytesWritable(bytes);
        }

        @Override
        public NullWritable getCurrentValue() throws IOException, InterruptedException {
          return NullWritable.get();
        }

        @Override
        public float getProgress() throws IOException, InterruptedException {
          return (float)(count / (double)numNodes);
        }

        @Override
        public void initialize(InputSplit arg0, TaskAttemptContext context)
            throws IOException, InterruptedException {
          numNodes = context.getConfiguration().getLong(GENERATOR_NUM_ROWS_PER_MAP_KEY, 25000000);
          // Use SecureRandom to avoid issue described in HBASE-13382.
          rand = new SecureRandom();
        }

        @Override
        public boolean nextKeyValue() throws IOException, InterruptedException {
          return count++ < numNodes;
        }

      }

      @Override
      public RecordReader<BytesWritable,NullWritable> createRecordReader(
          InputSplit split, TaskAttemptContext context) throws IOException, InterruptedException {
        GeneratorRecordReader rr = new GeneratorRecordReader();
        rr.initialize(split, context);
        return rr;
      }

      @Override
      public List<InputSplit> getSplits(JobContext job) throws IOException, InterruptedException {
        int numMappers = job.getConfiguration().getInt(GENERATOR_NUM_MAPPERS_KEY, 1);

        ArrayList<InputSplit> splits = new ArrayList<InputSplit>(numMappers);

        for (int i = 0; i < numMappers; i++) {
          splits.add(new GeneratorInputSplit());
        }

        return splits;
      }
    }

    /** Ensure output files from prev-job go to map inputs for current job */
    static class OneFilePerMapperSFIF<K, V> extends SequenceFileInputFormat<K, V> {
      @Override
      protected boolean isSplitable(JobContext context, Path filename) {
        return false;
      }
    }

    /**
     * Some ASCII art time:
     * [ . . . ] represents one batch of random longs of length WIDTH
     *
     *                _________________________
     *               |                  ______ |
     *               |                 |      ||
     *             __+_________________+_____ ||
     *             v v                 v     |||
     * first   = [ . . . . . . . . . . . ]   |||
     *             ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^     |||
     *             | | | | | | | | | | |     |||
     * prev    = [ . . . . . . . . . . . ]   |||
     *             ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^     |||
     *             | | | | | | | | | | |     |||
     * current = [ . . . . . . . . . . . ]   |||
     *                                       |||
     * ...                                   |||
     *                                       |||
     * last    = [ . . . . . . . . . . . ]   |||
     *             | | | | | | | | | | |-----|||
     *             |                 |--------||
     *             |___________________________|
     */
    static class GeneratorMapper
        extends Mapper<BytesWritable, NullWritable, NullWritable, NullWritable> {

      private byte[][] first = null;
      private byte[][] prev = null;
      private byte[][] current = null;
      private String id;
      private long rowId = 0;
      private int i;
      private KuduClient client;
      private KuduTable table;
      private KuduSession session;
      private KuduTable headsTable;
      private long numNodes;
      private long wrap;
      private int width;

      @Override
      protected void setup(Context context) throws IOException, InterruptedException {
        id = "Job: " + context.getJobID() + " Task: " + context.getTaskAttemptID();
        Configuration conf = context.getConfiguration();
        CommandLineParser parser = new CommandLineParser(conf);
        client = parser.getClient();
        try {
          table = client.openTable(getTableName(conf));
          headsTable = client.openTable(getHeadsTable(conf));
        } catch (Exception e) {
          throw new IOException(e);
        }
        session = client.newSession();
        session.setFlushMode(SessionConfiguration.FlushMode.MANUAL_FLUSH);
        session.setMutationBufferSpace(WIDTH_DEFAULT);
        session.setIgnoreAllDuplicateRows(true);

        this.width = context.getConfiguration().getInt(GENERATOR_WIDTH_KEY, WIDTH_DEFAULT);
        current = new byte[this.width][];
        int wrapMultiplier = context.getConfiguration().getInt(GENERATOR_WRAP_KEY, WRAP_DEFAULT);
        this.wrap = (long)wrapMultiplier * width;
        this.numNodes = context.getConfiguration().getLong(
            GENERATOR_NUM_ROWS_PER_MAP_KEY, (long)WIDTH_DEFAULT * WRAP_DEFAULT);
        if (this.numNodes < this.wrap) {
          this.wrap = this.numNodes;
        }
      }

      @Override
      protected void cleanup(Context context) throws IOException, InterruptedException {
        try {
          session.close();
          client.shutdown();
        } catch (Exception ex) {
          // ugh.
          throw new IOException(ex);
        }
      }

      @Override
      protected void map(BytesWritable key, NullWritable value, Context output) throws IOException {
        current[i] = new byte[key.getLength()];
        System.arraycopy(key.getBytes(), 0, current[i], 0, key.getLength());
        if (++i == current.length) {
          persist(output, current, false);
          i = 0;

          // Keep track of the first row so that we can point to it at the end.
          if (first == null) {
            first = current;
          }
          prev = current;
          current = new byte[this.width][];

          rowId += current.length;
          output.setStatus("Count " + rowId);

          // Check if it's time to wrap up this batch.
          if (rowId % wrap == 0) {
            // this block of code turns the 1 million linked list of length 25 into one giant
            // circular linked list of 25 million.
            circularLeftShift(first);

            persist(output, first, true);

            Operation insert = headsTable.newInsert();
            PartialRow row = insert.getRow();
            row.addLong(COLUMN_KEY_ONE,  Bytes.getLong(first[0]));
            row.addLong(COLUMN_KEY_TWO, Bytes.getLong(first[0], 8));
            try {
              session.apply(insert);
              session.flush();
            } catch (Exception e) {
              throw new IOException("Couldn't flush the head row, " + insert, e);
            }

            first = null;
            prev = null;
          }
        }
      }

      private static <T> void circularLeftShift(T[] first) {
        T ez = first[0];
        for (int i = 0; i < first.length - 1; i++)
          first[i] = first[i + 1];
        first[first.length - 1] = ez;
      }

      private void persist(Context output, byte[][] data, boolean update)
          throws IOException {
        try {
          for (int i = 0; i < data.length; i++) {
            Operation put = update ? table.newUpdate() : table.newInsert();
            PartialRow row = put.getRow();

            long keyOne = Bytes.getLong(data[i]);
            long keyTwo = Bytes.getLong(data[i], 8);

            row.addLong(COLUMN_KEY_ONE, keyOne);
            row.addLong(COLUMN_KEY_TWO, keyTwo);

            // prev is null for the first line, we'll update it at the end.
            if (prev == null) {
              row.setNull(COLUMN_PREV_ONE);
              row.setNull(COLUMN_PREV_TWO);
            } else {
              row.addLong(COLUMN_PREV_ONE, Bytes.getLong(prev[i]));
              row.addLong(COLUMN_PREV_TWO, Bytes.getLong(prev[i], 8));
            }

            if (!update) {
              // We only add those for new inserts, we don't update the heads with a new row, etc.
              row.addLong(COLUMN_ROW_ID, rowId + i);
              row.addString(COLUMN_CLIENT, id);
              row.addInt(COLUMN_UPDATE_COUNT, 0);
            }
            session.apply(put);

            if (i % 1000 == 0) {
              // Tickle progress every so often else maprunner will think us hung
              output.progress();
            }
          }

          session.flush();
        } catch (Exception ex) {
          throw new IOException(ex);
        }
      }
    }

    @Override
    public int run(String[] args) throws Exception {
      if (args.length < 4) {
        System.out.println("Usage : " + Generator.class.getSimpleName() +
            " <num mappers> <num nodes per map> <num_tablets> <tmp output dir> [<width> <wrap " +
            "multiplier>]");
        System.out.println("   where <num nodes per map> should be a multiple of " +
            " width*wrap multiplier, 25M by default");
        return 0;
      }

      int numMappers = Integer.parseInt(args[0]);
      long numNodes = Long.parseLong(args[1]);
      int numTablets = Integer.parseInt(args[2]);
      Path tmpOutput = new Path(args[3]);
      Integer width = (args.length < 5) ? null : Integer.parseInt(args[4]);
      Integer wrapMuplitplier = (args.length < 6) ? null : Integer.parseInt(args[5]);
      return run(numMappers, numNodes, numTablets, tmpOutput, width, wrapMuplitplier);
    }

    protected void createTables(int numTablets) throws Exception {

      createSchema(getTableName(getConf()), getTableSchema(), numTablets);
      createSchema(getHeadsTable(getConf()), getHeadsTableSchema(), numTablets);
    }

    protected void createSchema(String tableName, Schema schema, int numTablets) throws Exception {
      CommandLineParser parser = new CommandLineParser(getConf());
      KuduClient client = parser.getClient();
      try {
        if (numTablets < 1) {
          numTablets = 1;
        }

        if (client.tableExists(tableName)) {
          return;
        }

        CreateTableOptions builder =
            new CreateTableOptions().setNumReplicas(parser.getNumReplicas());
        if (numTablets > 1) {
          BigInteger min = BigInteger.valueOf(Long.MIN_VALUE);
          BigInteger max = BigInteger.valueOf(Long.MAX_VALUE);
          BigInteger step = max.multiply(BigInteger.valueOf(2)).divide(BigInteger.valueOf
              (numTablets));
          LOG.info(min.longValue());
          LOG.info(max.longValue());
          LOG.info(step.longValue());
          PartialRow splitRow = schema.newPartialRow();
          splitRow.addLong("key2", Long.MIN_VALUE);
          for (int i = 1; i < numTablets; i++) {
            long key = min.add(step.multiply(BigInteger.valueOf(i))).longValue();
            LOG.info("key " + key);
            splitRow.addLong("key1", key);
            builder.addSplitRow(splitRow);
          }
        }

        client.createTable(tableName, schema, builder);
      } finally {
        // Done with this client.
        client.shutdown();
      }
    }

    public int runRandomInputGenerator(int numMappers, long numNodes, Path tmpOutput,
                                       Integer width, Integer wrapMuplitplier) throws Exception {
      LOG.info("Running RandomInputGenerator with numMappers=" + numMappers
          + ", numNodes=" + numNodes);
      Job job = new Job(getConf());

      job.setJobName("Random Input Generator");
      job.setNumReduceTasks(0);
      job.setJarByClass(getClass());

      job.setInputFormatClass(GeneratorInputFormat.class);
      job.setOutputKeyClass(BytesWritable.class);
      job.setOutputValueClass(NullWritable.class);

      setJobConf(job, numMappers, numNodes, width, wrapMuplitplier);

      job.setMapperClass(Mapper.class); //identity mapper

      FileOutputFormat.setOutputPath(job, tmpOutput);
      job.setOutputFormatClass(SequenceFileOutputFormat.class);

      boolean success = job.waitForCompletion(true);

      return success ? 0 : 1;
    }

    public int runGenerator(int numMappers, long numNodes, int numTablets, Path tmpOutput,
                            Integer width, Integer wrapMuplitplier) throws Exception {
      LOG.info("Running Generator with numMappers=" + numMappers +", numNodes=" + numNodes);
      createTables(numTablets);

      Job job = new Job(getConf());

      job.setJobName("Link Generator");
      job.setNumReduceTasks(0);
      job.setJarByClass(getClass());

      FileInputFormat.setInputPaths(job, tmpOutput);
      job.setInputFormatClass(OneFilePerMapperSFIF.class);
      job.setOutputKeyClass(NullWritable.class);
      job.setOutputValueClass(NullWritable.class);

      setJobConf(job, numMappers, numNodes, width, wrapMuplitplier);

      job.setMapperClass(GeneratorMapper.class);

      job.setOutputFormatClass(NullOutputFormat.class);

      job.getConfiguration().setBoolean("mapred.map.tasks.speculative.execution", false);
      // If we fail, retrying will fail again in case we were able to flush at least once since
      // we'll be creating duplicate rows. Better to just have one try.
      job.getConfiguration().setInt("mapreduce.map.maxattempts", 1);
      // Lack of YARN-445 means we can't auto-jstack on timeout, so disabling the timeout gives
      // us a chance to do it manually.
      job.getConfiguration().setInt("mapreduce.task.timeout", 0);
      KuduTableMapReduceUtil.addDependencyJars(job);

      boolean success = job.waitForCompletion(true);

      return success ? 0 : 1;
    }

    public int run(int numMappers, long numNodes, int numTablets, Path tmpOutput,
                   Integer width, Integer wrapMuplitplier) throws Exception {
      int ret = runRandomInputGenerator(numMappers, numNodes, tmpOutput, width, wrapMuplitplier);
      if (ret > 0) {
        return ret;
      }
      return runGenerator(numMappers, numNodes, numTablets, tmpOutput, width, wrapMuplitplier);
    }
  }

  /**
   * A Map Reduce job that verifies that the linked lists generated by
   * {@link Generator} do not have any holes.
   */
  static class Verify extends Configured implements Tool {

    private static final Log LOG = LogFactory.getLog(Verify.class);
    private static final BytesWritable DEF = new BytesWritable(NO_KEY);
    private static final Joiner COMMA_JOINER = Joiner.on(",");
    private static final byte[] rowKey = new byte[ROWKEY_LENGTH];
    private static final byte[] prev = new byte[ROWKEY_LENGTH];

    private Job job;

    public static class VerifyMapper extends Mapper<NullWritable, RowResult,
        BytesWritable, BytesWritable> {
      private BytesWritable row = new BytesWritable();
      private BytesWritable ref = new BytesWritable();

      @Override
      protected void map(NullWritable key, RowResult value, Mapper.Context context)
          throws IOException ,InterruptedException {
        Bytes.setLong(rowKey, value.getLong(0));
        Bytes.setLong(rowKey, value.getLong(1), 8);

        row.set(rowKey, 0, rowKey.length);
        // Emit that the row is defined
        context.write(row, DEF);
        if (value.isNull(2)) {
          LOG.warn(String.format("Prev is not set for: %s", Bytes.pretty(rowKey)));
        } else {
          Bytes.setLong(prev, value.getLong(2));
          Bytes.setLong(prev, value.getLong(3), 8);
          ref.set(prev, 0, prev.length);
          // Emit which row is referenced by this row.
          context.write(ref, row);
        }
      }
    }

    public enum Counts {
      UNREFERENCED, UNDEFINED, REFERENCED, EXTRAREFERENCES
    }

    public static class VerifyReducer extends Reducer<BytesWritable,BytesWritable,Text,Text> {
      private ArrayList<byte[]> refs = new ArrayList<byte[]>();

      private AtomicInteger rows = new AtomicInteger(0);

      @Override
      public void reduce(BytesWritable key, Iterable<BytesWritable> values, Context context)
          throws IOException, InterruptedException {

        int defCount = 0;

        refs.clear();
        // We only expect two values, a DEF and a reference, but there might be more.
        for (BytesWritable type : values) {
          if (type.getLength() == DEF.getLength()) {
            defCount++;
          } else {
            byte[] bytes = new byte[type.getLength()];
            System.arraycopy(type.getBytes(), 0, bytes, 0, type.getLength());
            refs.add(bytes);
          }
        }

        // TODO check for more than one def, should not happen

        List<String> refsList = new ArrayList<>(refs.size());
        String keyString = null;
        if (defCount == 0 || refs.size() != 1) {
          for (byte[] ref : refs) {
            refsList.add(COMMA_JOINER.join(Bytes.getLong(ref), Bytes.getLong(ref, 8)));
          }
          keyString = COMMA_JOINER.join(Bytes.getLong(key.getBytes()),
              Bytes.getLong(key.getBytes(), 8));

          LOG.error("Linked List error: Key = " + keyString + " References = " + refsList);
        }

        if (defCount == 0 && refs.size() > 0) {
          // this is bad, found a node that is referenced but not defined. It must have been
          // lost, emit some info about this node for debugging purposes.
          context.write(new Text(keyString), new Text(refsList.toString()));
          context.getCounter(Counts.UNDEFINED).increment(1);
        } else if (defCount > 0 && refs.size() == 0) {
          // node is defined but not referenced
          context.write(new Text(keyString), new Text("none"));
          context.getCounter(Counts.UNREFERENCED).increment(1);
        } else {
          if (refs.size() > 1) {
            if (refsList != null) {
              context.write(new Text(keyString), new Text(refsList.toString()));
            }
            context.getCounter(Counts.EXTRAREFERENCES).increment(refs.size() - 1);
          }
          // node is defined and referenced
          context.getCounter(Counts.REFERENCED).increment(1);
        }

      }
    }

    @Override
    public int run(String[] args) throws Exception {

      if (args.length != 2) {
        System.out.println("Usage : " + Verify.class.getSimpleName() + " <output dir> <num reducers>");
        return 0;
      }

      String outputDir = args[0];
      int numReducers = Integer.parseInt(args[1]);

      return run(outputDir, numReducers);
    }

    public int run(String outputDir, int numReducers) throws Exception {
      return run(new Path(outputDir), numReducers);
    }

    public int run(Path outputDir, int numReducers) throws Exception {
      LOG.info("Running Verify with outputDir=" + outputDir +", numReducers=" + numReducers);

      job = new Job(getConf());

      job.setJobName("Link Verifier");
      job.setNumReduceTasks(numReducers);
      job.setJarByClass(getClass());

      Joiner columnsToQuery = Joiner.on(",");

      new KuduTableMapReduceUtil.TableInputFormatConfiguratorWithCommandLineParser(
          job, getTableName(getConf()),
          columnsToQuery.join(COLUMN_KEY_ONE, COLUMN_KEY_TWO, COLUMN_PREV_ONE, COLUMN_PREV_TWO))
          .configure();
      job.setMapperClass(VerifyMapper.class);
      job.setMapOutputKeyClass(BytesWritable.class);
      job.setMapOutputValueClass(BytesWritable.class);
      job.getConfiguration().setBoolean("mapred.map.tasks.speculative.execution", false);

      job.setReducerClass(VerifyReducer.class);
      job.setOutputFormatClass(TextOutputFormat.class);
      TextOutputFormat.setOutputPath(job, outputDir);

      boolean success = job.waitForCompletion(true);

      return success ? 0 : 1;
    }

    @SuppressWarnings("deprecation")
    public boolean verify(long expectedReferenced) throws Exception {
      if (job == null) {
        throw new IllegalStateException("You should call run() first");
      }

      Counters counters = job.getCounters();

      Counter referenced = counters.findCounter(Counts.REFERENCED);
      Counter unreferenced = counters.findCounter(Counts.UNREFERENCED);
      Counter undefined = counters.findCounter(Counts.UNDEFINED);
      Counter multiref = counters.findCounter(Counts.EXTRAREFERENCES);

      boolean success = true;
      //assert
      if (expectedReferenced != referenced.getValue()) {
        LOG.error("Expected referenced count does not match with actual referenced count. " +
            "Expected referenced=" + expectedReferenced + ", actual=" + referenced.getValue());
        success = false;
      }

      if (unreferenced.getValue() > 0) {
        boolean couldBeMultiRef = (multiref.getValue() == unreferenced.getValue());
        LOG.error("Unreferenced nodes were not expected. Unreferenced count=" + unreferenced.getValue()
            + (couldBeMultiRef ? "; could be due to duplicate random numbers" : ""));
        success = false;
      }

      if (undefined.getValue() > 0) {
        LOG.error("Found an undefined node. Undefined count=" + undefined.getValue());
        success = false;
      }

      // TODO Add the rows' location on failure.
      if (!success) {
        //Configuration conf = job.getConfiguration();
        //HConnection conn = HConnectionManager.getConnection(conf);
        //TableName tableName = getTableName(conf);
        CounterGroup g = counters.getGroup("undef");
        Iterator<Counter> it = g.iterator();
        while (it.hasNext()) {
          String keyString = it.next().getName();
          //byte[] key = Bytes.toBytes(keyString);
          //HRegionLocation loc = conn.relocateRegion(tableName, key);
          LOG.error("undefined row " + keyString /*+ ", " + loc*/);
        }
        g = counters.getGroup("unref");
        it = g.iterator();
        while (it.hasNext()) {
          String keyString = it.next().getName();
          //byte[] key = Bytes.toBytes(keyString);
          //HRegionLocation loc = conn.relocateRegion(tableName, key);
          LOG.error("unreferred row " + keyString /*+ ", " + loc*/);
        }
      }
      return success;
    }
  }

  /**
   * Executes Generate and Verify in a loop. Data is not cleaned between runs, so each iteration
   * adds more data.
   */
  static class Loop extends Configured implements Tool {

    private static final Log LOG = LogFactory.getLog(Loop.class);

    IntegrationTestBigLinkedList it;

    FileSystem fs;

    protected void runGenerator(int numMappers, long numNodes, int numTablets,
                                String outputDir, Integer width, Integer wrapMuplitplier) throws Exception {
      Path outputPath = new Path(outputDir);
      UUID uuid = UUID.randomUUID(); //create a random UUID.
      Path generatorOutput = new Path(outputPath, uuid.toString());

      Generator generator = new Generator();
      generator.setConf(getConf());
      int retCode = generator.run(numMappers, numNodes, numTablets, generatorOutput, width,
          wrapMuplitplier);
      if (retCode > 0) {
        throw new RuntimeException("Generator failed with return code: " + retCode);
      }
      fs.delete(generatorOutput, true);
    }

    protected void runVerify(String outputDir,
                             int numReducers,
                             long expectedNumNodes,
                             int retries) throws Exception {
      // Kudu doesn't fully support snapshot consistency so we might start reading from a node that
      // doesn't have all the data. This happens often with under "chaos monkey"-type of setups.
      for (int i = 0; i < retries; i++) {
        if (i > 0) {
          long sleep = 60 * 1000;
          LOG.info("Retrying in " + sleep + "ms");
          Thread.sleep(sleep);
        }

        Path outputPath = new Path(outputDir);
        UUID uuid = UUID.randomUUID(); //create a random UUID.
        Path iterationOutput = new Path(outputPath, uuid.toString());

        Verify verify = new Verify();
        verify.setConf(getConf());
        int retCode = verify.run(iterationOutput, numReducers);
        if (retCode > 0) {
          LOG.warn("Verify.run failed with return code: " + retCode);
        } else if (!verify.verify(expectedNumNodes)) {
          LOG.warn("Verify.verify failed");
        } else {
          fs.delete(iterationOutput, true);
          LOG.info("Verify finished with success. Total nodes=" + expectedNumNodes);
        }
      }
      throw new RuntimeException("Ran out of retries to verify");
    }

    @Override
    public int run(String[] args) throws Exception {
      if (args.length < 6) {
        System.err.println("Usage: Loop <num iterations> <num mappers> <num nodes per mapper> " +
            "<output dir> <num reducers> [<width> <wrap multiplier> <start expected nodes>" +
            "<num_verify_retries>]");
        return 1;
      }
      LOG.info("Running Loop with args:" + Arrays.deepToString(args));

      int numIterations = Integer.parseInt(args[0]);
      int numMappers = Integer.parseInt(args[1]);
      long numNodes = Long.parseLong(args[2]);
      int numTablets = Integer.parseInt(args[3]);
      String outputDir = args[4];
      int numReducers = Integer.parseInt(args[5]);
      int width = (args.length < 6) ? null : Integer.parseInt(args[6]);
      int wrapMuplitplier = (args.length < 8) ? null : Integer.parseInt(args[7]);
      long expectedNumNodes = (args.length < 9) ? 0 : Long.parseLong(args[8]);
      int numVerifyRetries = (args.length < 10) ? 3 : Integer.parseInt(args[9]);

      if (numIterations < 0) {
        numIterations = Integer.MAX_VALUE; // run indefinitely (kind of)
      }

      fs = FileSystem.get(getConf());

      for (int i = 0; i < numIterations; i++) {
        LOG.info("Starting iteration = " + i);
        runGenerator(numMappers, numNodes, numTablets, outputDir, width, wrapMuplitplier);
        expectedNumNodes += numMappers * numNodes;

        runVerify(outputDir, numReducers, expectedNumNodes, numVerifyRetries);
      }

      return 0;
    }
  }

  /**
   * A stand alone program that prints out portions of a list created by {@link Generator}
   */
  private static class Print extends Configured implements Tool {
    @Override
    public int run(String[] args) throws Exception {
      Options options = new Options();
      options.addOption("s", "start", true, "start key, only the first component");
      options.addOption("e", "end", true, "end key (exclusive), only the first component");
      options.addOption("l", "limit", true, "number to print");

      GnuParser parser = new GnuParser();
      CommandLine cmd = null;
      try {
        cmd = parser.parse(options, args);
        if (cmd.getArgs().length != 0) {
          throw new ParseException("Command takes no arguments");
        }
      } catch (ParseException e) {
        System.err.println("Failed to parse command line " + e.getMessage());
        System.err.println();
        HelpFormatter formatter = new HelpFormatter();
        formatter.printHelp(getClass().getSimpleName(), options);
        System.exit(-1);
      }

      CommandLineParser cmdLineParser = new CommandLineParser(getConf());
      long timeout = cmdLineParser.getOperationTimeoutMs();
      KuduClient client = cmdLineParser.getClient();

      KuduTable table = client.openTable(getTableName(getConf()));
      KuduScanner.KuduScannerBuilder builder =
          client.newScannerBuilder(table)
              .scanRequestTimeout(timeout);


      if (cmd.hasOption("s")) {
        PartialRow row = table.getSchema().newPartialRow();
        row.addLong(0, Long.parseLong(cmd.getOptionValue("s")));
        builder.lowerBound(row);
      }
      if (cmd.hasOption("e")) {
        PartialRow row = table.getSchema().newPartialRow();
        row.addLong(0, Long.parseLong(cmd.getOptionValue("e")));
        builder.exclusiveUpperBound(row);
      }

      int limit = cmd.hasOption("l") ? Integer.parseInt(cmd.getOptionValue("l")) : 100;

      int count = 0;

      KuduScanner scanner = builder.build();
      while (scanner.hasMoreRows() && count < limit) {
        RowResultIterator rowResults = scanner.nextRows();
        count = printNodesAndGetNewCount(count, limit, rowResults);
      }
      RowResultIterator rowResults = scanner.close();
      printNodesAndGetNewCount(count, limit, rowResults);

      client.shutdown();

      return 0;
    }

    private static int printNodesAndGetNewCount(int oldCount, int limit,
                                                RowResultIterator rowResults) {
      int newCount = oldCount;
      if (rowResults == null) {
        return newCount;
      }

      CINode node = new CINode();
      for (RowResult result : rowResults) {
        newCount++;
        node = getCINode(result, node);
        printCINodeString(node);
        if (newCount == limit) {
          break;
        }
      }
      return newCount;
    }
  }

  /**
   * This tool needs to be run separately from the Generator-Verify loop. It can run while the
   * other two are running or in between loops.
   *
   * Each mapper scans a "heads" table and, for each row, follows the circular linked list and
   * updates their counter until it reaches the head of the list again.
   */
  private static class Updater extends Configured implements Tool {

    private static final Log LOG = LogFactory.getLog(Updater.class);

    private static final String MAX_LINK_UPDATES_PER_MAPPER = "kudu.updates.per.mapper";

    public enum Counts {
      // Stats on what we're updating.
      UPDATED_LINKS,
      UPDATED_NODES,
      FIRST_UPDATE,
      SECOND_UPDATE,
      THIRD_UPDATE,
      FOURTH_UPDATE,
      MORE_THAN_FOUR_UPDATES,
      // Stats on what's broken.
      BROKEN_LINKS,
      BAD_UPDATE_COUNTS
    }

    public static class UpdaterMapper extends Mapper<NullWritable, RowResult,
        NullWritable, NullWritable> {
      private KuduClient client;
      private KuduTable table;
      private KuduSession session;

      /**
       * Schema we use when getting rows from the linked list, we only need the reference and
       * its update count.
       */
      private final List<String> SCAN_COLUMN_NAMES = ImmutableList.of(
          COLUMN_PREV_ONE, COLUMN_PREV_TWO, COLUMN_UPDATE_COUNT, COLUMN_CLIENT);

      private long numUpdatesPerMapper;

      /**
       * Processing each linked list takes minutes, meaning that it's easily possible for our
       * scanner to timeout. Instead, we gather all the linked list heads that we need and
       * process them all at once in the first map invocation.
       */
      private List<Pair<Long, Long>> headsCache;

      @Override
      protected void setup(Context context) throws IOException, InterruptedException {
        Configuration conf = context.getConfiguration();
        CommandLineParser parser = new CommandLineParser(conf);
        client = parser.getClient();
        try {
          table = client.openTable(getTableName(conf));
        } catch (Exception e) {
          throw new IOException("Couldn't open the linked list table", e);
        }
        session = client.newSession();

        Schema tableSchema = table.getSchema();


        numUpdatesPerMapper = conf.getLong(MAX_LINK_UPDATES_PER_MAPPER, 1);
        headsCache = new ArrayList<Pair<Long, Long>>((int)numUpdatesPerMapper);
      }

      @Override
      protected void map(NullWritable key, RowResult value, Mapper.Context context)
          throws IOException, InterruptedException {
        // Add as many heads as we need, then we skip the rest.
        do {
          if (headsCache.size() < numUpdatesPerMapper) {
            value = (RowResult)context.getCurrentValue();
            headsCache.add(new Pair<Long, Long>(value.getLong(0), value.getLong(1)));
          }
        } while (context.nextKeyValue());

        // At this point we've exhausted the scanner and hopefully gathered all the linked list
        // heads we needed.
        LOG.info("Processing " + headsCache.size() +
            " linked lists, out of " + numUpdatesPerMapper);
        processAllHeads(context);
      }

      private void processAllHeads(Mapper.Context context) throws IOException {
        for (Pair<Long, Long> value : headsCache) {
          processHead(value, context);
        }
      }

      private void processHead(Pair<Long, Long> head, Mapper.Context context) throws IOException {
        long headKeyOne = head.getFirst();
        long headKeyTwo = head.getSecond();
        long prevKeyOne = headKeyOne;
        long prevKeyTwo = headKeyTwo;
        int currentCount = -1;
        int newCount = -1;
        String client = null;

        // Always printing this out, really useful when debugging.
        LOG.info("Head: " + getStringFromKeys(headKeyOne, headKeyTwo));

        do {
          RowResult prev = nextNode(prevKeyOne, prevKeyTwo);
          if (prev == null) {
            context.getCounter(Counts.BROKEN_LINKS).increment(1);
            LOG.warn(getStringFromKeys(prevKeyOne, prevKeyTwo) + " doesn't exist");
            break;
          }

          // It's possible those columns are null, let's not break trying to read them.
          if (prev.isNull(0) || prev.isNull(1)) {
            context.getCounter(Counts.BROKEN_LINKS).increment(1);
            LOG.warn(getStringFromKeys(prevKeyOne, prevKeyTwo) + " isn't referencing anywhere");
            break;
          }

          int prevCount = prev.getInt(2);
          String prevClient = prev.getString(3);
          if (currentCount == -1) {
            // First time we loop we discover what the count was and set the new one.
            currentCount = prevCount;
            newCount = currentCount + 1;
            client = prevClient;
          }

          if (prevCount != currentCount) {
            context.getCounter(Counts.BAD_UPDATE_COUNTS).increment(1);
            LOG.warn(getStringFromKeys(prevKeyOne, prevKeyTwo) + " has a wrong updateCount, " +
                prevCount + " instead of " + currentCount);
            // Game over, there's corruption.
            break;
          }

          if (!prevClient.equals(client)) {
            context.getCounter(Counts.BROKEN_LINKS).increment(1);
            LOG.warn(getStringFromKeys(prevKeyOne, prevKeyTwo) + " has the wrong client, " +
                "bad reference? Bad client= " + prevClient);
            break;
          }

          updateRow(prevKeyOne, prevKeyTwo, newCount);
          context.getCounter(Counts.UPDATED_NODES).increment(1);
          if (prevKeyOne % 10 == 0) {
            context.progress();
          }
          prevKeyOne = prev.getLong(0);
          prevKeyTwo = prev.getLong(1);
        } while (headKeyOne != prevKeyOne && headKeyTwo != prevKeyTwo);

        updateStatCounters(context, newCount);
        context.getCounter(Counts.UPDATED_LINKS).increment(1);
      }

      /**
       * Finds the next node in the linked list.
       */
      private RowResult nextNode(long prevKeyOne, long prevKeyTwo) throws IOException {
        KuduScanner.KuduScannerBuilder builder = client.newScannerBuilder(table)
          .setProjectedColumnNames(SCAN_COLUMN_NAMES);

        configureScannerForRandomRead(builder, table, prevKeyOne, prevKeyTwo);

        try {
          return getOneRowResult(builder.build());
        } catch (Exception e) {
          // Goes right out and fails the job.
          throw new IOException("Couldn't read the following row: " +
              getStringFromKeys(prevKeyOne, prevKeyTwo), e);
        }
      }

      private void updateRow(long keyOne, long keyTwo, int newCount) throws IOException {
        Update update = table.newUpdate();
        PartialRow row = update.getRow();
        row.addLong(COLUMN_KEY_ONE, keyOne);
        row.addLong(COLUMN_KEY_TWO, keyTwo);
        row.addInt(COLUMN_UPDATE_COUNT, newCount);
        try {
          session.apply(update);
        } catch (Exception e) {
          // Goes right out and fails the job.
          throw new IOException("Couldn't update the following row: " +
              getStringFromKeys(keyOne, keyTwo), e);
        }
      }

      /**
       * We keep some statistics about the linked list we update so that we can get a feel of
       * what's being updated.
       */
      private void updateStatCounters(Mapper.Context context, int newCount) {
        switch (newCount) {
          case -1:
          case 0:
            // TODO We didn't event get the first node?
            break;
          case 1:
            context.getCounter(Counts.FIRST_UPDATE).increment(1);
            break;
          case 2:
            context.getCounter(Counts.SECOND_UPDATE).increment(1);
            break;
          case 3:
            context.getCounter(Counts.THIRD_UPDATE).increment(1);
            break;
          case 4:
            context.getCounter(Counts.FOURTH_UPDATE).increment(1);
            break;
          default:
            context.getCounter(Counts.MORE_THAN_FOUR_UPDATES).increment(1);
            break;
        }
      }

      @Override
      protected void cleanup(Context context) throws IOException, InterruptedException {
        try {
          session.close();
          client.shutdown();
        } catch (Exception ex) {
          // Goes right out and fails the job.
          throw new IOException("Coulnd't close the scanner after the task completed", ex);
        }
      }
    }

    public int run(long maxLinkUpdatesPerMapper) throws Exception {
      LOG.info("Running Updater with maxLinkUpdatesPerMapper=" + maxLinkUpdatesPerMapper);

      Job job = new Job(getConf());

      job.setJobName("Link Updater");
      job.setNumReduceTasks(0);
      job.setJarByClass(getClass());

      Joiner columnsToQuery = Joiner.on(",");

      new KuduTableMapReduceUtil.TableInputFormatConfiguratorWithCommandLineParser(
          job, getHeadsTable(getConf()),
          columnsToQuery.join(COLUMN_KEY_ONE, COLUMN_KEY_TWO))
          .configure();

      job.setMapperClass(UpdaterMapper.class);
      job.setMapOutputKeyClass(BytesWritable.class);
      job.setMapOutputValueClass(BytesWritable.class);
      job.getConfiguration().setBoolean("mapred.map.tasks.speculative.execution", false);
      // If something fails we want to exit ASAP.
      job.getConfiguration().setInt("mapreduce.map.maxattempts", 1);
      // Lack of YARN-445 means we can't auto-jstack on timeout, so disabling the timeout gives
      // us a chance to do it manually.
      job.getConfiguration().setInt("mapreduce.task.timeout", 0);
      job.getConfiguration().setLong(MAX_LINK_UPDATES_PER_MAPPER, maxLinkUpdatesPerMapper);

      job.setOutputKeyClass(NullWritable.class);
      job.setOutputValueClass(NullWritable.class);
      job.setOutputFormatClass(NullOutputFormat.class);

      KuduTableMapReduceUtil.addDependencyJars(job);

      boolean success = job.waitForCompletion(true);

      Counters counters = job.getCounters();

      if (success) {
        // Let's not continue looping if we have broken linked lists.
        Counter brokenLinks = counters.findCounter(Counts.BROKEN_LINKS);
        Counter badUpdates = counters.findCounter(Counts.BAD_UPDATE_COUNTS);
        if (brokenLinks.getValue() > 0 || badUpdates.getValue() > 0) {
          LOG.error("Corruption was detected, see the job's counters. Ending the update loop.");
          success = false;
        }
      }
      return success ? 0 : 1;
    }

    @Override
    public int run(String[] args) throws Exception {
      if (args.length < 2) {
        System.err.println("Usage: Update <num iterations> <max link updates per mapper>");
        System.err.println(" where <num iterations> will be 'infinite' if passed a negative value" +
            " or zero");
        return 1;
      }
      LOG.info("Running Loop with args:" + Arrays.deepToString(args));

      int numIterations = Integer.parseInt(args[0]);
      long maxUpdates = Long.parseLong(args[1]);

      if (numIterations <= 0) {
        numIterations = Integer.MAX_VALUE;
      }

      if (maxUpdates < 1) {
        maxUpdates = 1;
      }

      for (int i = 0; i < numIterations; i++) {
        LOG.info("Starting iteration = " + i);
        int ret = run(maxUpdates);
        if (ret != 0) {
          LOG.error("Can't continue updating, last run failed.");
          return ret;
        }
      }

      return 0;
    }
  }

  /**
   * A stand alone program that deletes a single node.
   * TODO
   */
  /*private static class Delete extends Configured implements Tool {
    @Override
    public int run(String[] args) throws Exception {
      if (args.length != 1) {
        System.out.println("Usage : " + Delete.class.getSimpleName() + " <node to delete>");
        return 0;
      }
      byte[] val = Bytes.toBytesBinary(args[0]);

      org.apache.hadoop.hbase.client.Delete delete
          = new org.apache.hadoop.hbase.client.Delete(val);

      HTable table = new HTable(getConf(), getTableName(getConf()));

      table.delete(delete);
      table.flushCommits();
      table.close();

      System.out.println("Delete successful");
      return 0;
    }
  }*/

  /**
   * A stand alone program that follows a linked list created by {@link Generator}
   * and prints timing info.
   *
   */
  private static class Walker extends Configured implements Tool {

    private KuduClient client;
    private KuduTable table;

    @Override
    public int run(String[] args) throws IOException {
      if (args.length < 1) {
        System.err.println("Usage: Walker <start key> [<num nodes>]");
        System.err.println(" where <num nodes> defaults to 100 nodes that will be printed out");
        return 1;
      }
      int maxNumNodes = 100;
      if (args.length == 2) {
        maxNumNodes = Integer.parseInt(args[1]);
      }
      System.out.println("Running Walker with args:" + Arrays.deepToString(args));

      String[] keys = args[0].split(",");
      if (keys.length != 2) {
        System.err.println("The row key must be formatted like key1,key2");
        return 1;
      }

      long keyOne = Long.parseLong(keys[0]);
      long keyTwo = Long.parseLong(keys[1]);

      System.out.println("Walking with " + getStringFromKeys(keyOne, keyTwo));

      try {
        walk(keyOne, keyTwo, maxNumNodes);
      } catch (Exception e) {
        throw new IOException(e);
      }
      return 0;
    }

    private void walk(long headKeyOne, long headKeyTwo, int maxNumNodes) throws Exception {
      CommandLineParser parser = new CommandLineParser(getConf());
      client = parser.getClient();
      table = client.openTable(getTableName(getConf()));

      long prevKeyOne = headKeyOne;
      long prevKeyTwo = headKeyTwo;
      CINode node = new CINode();
      int nodesCount = 0;

      do {
        RowResult rr = nextNode(prevKeyOne, prevKeyTwo);
        if (rr == null) {
          System.err.println(getStringFromKeys(prevKeyOne, prevKeyTwo) + " doesn't exist!");
          break;
        }
        getCINode(rr, node);
        printCINodeString(node);
        if (rr.isNull(2) || rr.isNull(3)) {
          System.err.println("Last node didn't have a reference, breaking");
          break;
        }
        prevKeyOne = rr.getLong(2);
        prevKeyTwo = rr.getLong(3);
        nodesCount++;
      } while ((headKeyOne != prevKeyOne && headKeyTwo != prevKeyTwo) && (nodesCount <
          maxNumNodes));
    }

    private RowResult nextNode(long keyOne, long keyTwo) throws Exception {
      KuduScanner.KuduScannerBuilder builder = client.newScannerBuilder(table);
      configureScannerForRandomRead(builder, table, keyOne, keyTwo);

      return getOneRowResult(builder.build());
    }
  }

  private static void configureScannerForRandomRead(AbstractKuduScannerBuilder builder,
                                                    KuduTable table,
                                                    long keyOne,
                                                    long keyTwo) {
    PartialRow lowerBound = table.getSchema().newPartialRow();
    lowerBound.addLong(0, keyOne);
    lowerBound.addLong(1, keyTwo);
    builder.lowerBound(lowerBound);

    PartialRow upperBound = table.getSchema().newPartialRow();
    // Adding 1 since we want a single row, and the upper bound is exclusive.
    upperBound.addLong(0, keyOne + 1);
    upperBound.addLong(1, keyTwo + 1);
    builder.exclusiveUpperBound(upperBound);
  }

  private static String getTableName(Configuration conf) {
    return conf.get(TABLE_NAME_KEY, DEFAULT_TABLE_NAME);
  }

  private static String getHeadsTable(Configuration conf) {
    return conf.get(HEADS_TABLE_NAME_KEY, DEFAULT_HEADS_TABLE_NAME);
  }

  private static CINode getCINode(RowResult result, CINode node) {

    node.key = getStringFromKeys(result.getLong(0), result.getLong(1));
    if (result.isNull(2) || result.isNull(3)) {
      node.prev = "NO_REFERENCE";
    } else {
      node.prev = getStringFromKeys(result.getLong(2), result.getLong(3));
    }
    node.rowId = result.getInt(4);
    node.client = result.getString(5);
    node.updateCount = result.getInt(6);
    return node;
  }

  private static void printCINodeString(CINode node) {
    System.out.printf("%s:%s:%012d:%s:%s\n", node.key, node.prev, node.rowId, node.client,
        node.updateCount);
  }

  private static String getStringFromKeys(long key1, long key2) {
    return new StringBuilder().append(key1).append(",").append(key2).toString();
  }

  private static RowResult getOneRowResult(KuduScanner scanner) throws Exception {
    RowResultIterator rowResults;
    rowResults = scanner.nextRows();
    if (rowResults.getNumRows() == 0) {
      return null;
    }
    if (rowResults.getNumRows() > 1) {
      throw new Exception("Received too many rows from scanner " + scanner);
    }
    return rowResults.next();
  }

  private void usage() {
    System.err.println("Usage: " + this.getClass().getSimpleName() + " COMMAND [COMMAND options]");
    System.err.println("  where COMMAND is one of:");
    System.err.println("");
    System.err.println("  Generator                  A map only job that generates data.");
    System.err.println("  Verify                     A map reduce job that looks for holes");
    System.err.println("                             Look at the counts after running");
    System.err.println("                             REFERENCED and UNREFERENCED are ok");
    System.err.println("                             any UNDEFINED counts are bad. Do not");
    System.err.println("                             run at the same time as the Generator.");
    System.err.println("  Print                      A standalone program that prints nodes");
    System.err.println("                             in the linked list.");
    System.err.println("  Loop                       A program to Loop through Generator and");
    System.err.println("                             Verify steps");
    System.err.println("  Update                     A program to updade the nodes");
    /* System.err.println("  Delete                     A standalone program that deletes a");
    System.err.println("                             single node.");*/
    System.err.println("  Walker                     A standalong program that starts ");
    System.err.println("                             following a linked list");
    System.err.println("\t  ");
    System.err.flush();
  }

  protected void processOptions(String[] args) {
    //get the class, run with the conf
    if (args.length < 1) {
      usage();
      throw new RuntimeException("Incorrect Number of args.");
    }
    toRun = args[0];
    otherArgs = Arrays.copyOfRange(args, 1, args.length);
  }

  @Override
  public int run(String[] args) throws Exception {
    Tool tool = null;
    processOptions(args);
    if (toRun.equals("Generator")) {
      tool = new Generator();
    } else if (toRun.equals("Verify")) {
      tool = new Verify();
    } else if (toRun.equals("Loop")) {
      Loop loop = new Loop();
      loop.it = this;
      tool = loop;

    } else if (toRun.equals("Print")) {
      tool = new Print();
    } else if (toRun.equals("Update")) {
      tool = new Updater();
    } else if (toRun.equals("Walker")) {
      tool = new Walker();
    } /*else if (toRun.equals("Delete")) {
      tool = new Delete();
    }*/ else {
      usage();
      throw new RuntimeException("Unknown arg");
    }

    return ToolRunner.run(getConf(), tool, otherArgs);
  }

  private static void setJobConf(Job job, int numMappers, long numNodes,
                                 Integer width, Integer wrapMultiplier) {
    job.getConfiguration().setInt(GENERATOR_NUM_MAPPERS_KEY, numMappers);
    job.getConfiguration().setLong(GENERATOR_NUM_ROWS_PER_MAP_KEY, numNodes);
    if (width != null) {
      job.getConfiguration().setInt(GENERATOR_WIDTH_KEY, width);
    }
    if (wrapMultiplier != null) {
      job.getConfiguration().setInt(GENERATOR_WRAP_KEY, wrapMultiplier);
    }
  }

  public static void main(String[] args) throws Exception {
    int ret = ToolRunner.run(new IntegrationTestBigLinkedList(), args);
    System.exit(ret);
  }
}
