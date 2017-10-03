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

import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.kududb.mapreduce.CommandLineParser;
import org.kududb.mapreduce.KuduTableMapReduceUtil;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.conf.Configured;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.mapreduce.lib.input.FileInputFormat;
import org.apache.hadoop.mapreduce.lib.input.TextInputFormat;
import org.apache.hadoop.util.Tool;
import org.apache.hadoop.util.ToolRunner;

import java.io.IOException;

/**
 * Map-only job that reads CSV files and inserts them into a single Kudu table.
 */
@InterfaceAudience.Public
@InterfaceStability.Unstable
public class ImportCsv extends Configured implements Tool {

  public static enum Counters { BAD_LINES };

  static final String NAME = "importcsv";
  static final String DEFAULT_SEPARATOR = "\t";
  static final String SEPARATOR_CONF_KEY = "importcsv.separator";
  static final String JOB_NAME_CONF_KEY = "importcsv.job.name";
  static final String SKIP_LINES_CONF_KEY = "importcsv.skip.bad.lines";
  static final String COLUMNS_NAMES_KEY = "importcsv.column.names";

  /**
   * Sets up the actual job.
   *
   * @param conf The current configuration.
   * @param args The command line parameters.
   * @return The newly created job.
   * @throws java.io.IOException When setting up the job fails.
   */
  @SuppressWarnings("deprecation")
  public static Job createSubmittableJob(Configuration conf, String[] args)
      throws IOException, ClassNotFoundException {

    Class<ImportCsvMapper> mapperClass = ImportCsvMapper.class;
    conf.set(COLUMNS_NAMES_KEY, args[0]);
    String tableName = args[1];
    Path inputDir = new Path(args[2]);

    String jobName = conf.get(JOB_NAME_CONF_KEY, NAME + "_" + tableName);
    Job job = new Job(conf, jobName);
    job.setJarByClass(mapperClass);
    FileInputFormat.setInputPaths(job, inputDir);
    job.setInputFormatClass(TextInputFormat.class);
    job.setMapperClass(mapperClass);
    job.setNumReduceTasks(0);
    new KuduTableMapReduceUtil.TableOutputFormatConfiguratorWithCommandLineParser(
        job,
        tableName)
        .configure();
    return job;
  }

  /*
   * @param errorMsg Error message. Can be null.
   */
  private static void usage(final String errorMsg) {
    if (errorMsg != null && errorMsg.length() > 0) {
      System.err.println("ERROR: " + errorMsg);
    }
    String usage =
        "Usage: " + NAME + " <colAa,colB,colC> <table.name> <input.dir>\n\n" +
            "Imports the given input directory of CSV data into the specified table.\n" +
            "\n" +
            "The column names of the CSV data must be specified in the form of " +
            "comma-separated column names.\n" +
            "Other options that may be specified with -D include:\n" +
            "  -D" + SKIP_LINES_CONF_KEY + "=false - fail if encountering an invalid line\n" +
            "  '-D" + SEPARATOR_CONF_KEY + "=|' - eg separate on pipes instead of tabs\n" +
            "  -D" + JOB_NAME_CONF_KEY + "=jobName - use the specified mapreduce job name for the" +
            " import.\n" +
            CommandLineParser.getHelpSnippet();

    System.err.println(usage);
  }

  @Override
  public int run(String[] otherArgs) throws Exception {
    if (otherArgs.length < 3) {
      usage("Wrong number of arguments: " + otherArgs.length);
      return -1;
    }
    Job job = createSubmittableJob(getConf(), otherArgs);
    return job.waitForCompletion(true) ? 0 : 1;
  }

  public static void main(String[] args) throws Exception {
    int status = ToolRunner.run(new ImportCsv(), args);
    System.exit(status);
  }
}
