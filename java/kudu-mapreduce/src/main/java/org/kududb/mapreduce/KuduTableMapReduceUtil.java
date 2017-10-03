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
package org.kududb.mapreduce;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.commons.net.util.Base64;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.io.NullWritable;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.mapreduce.TaskInputOutputContext;
import org.apache.hadoop.util.JarFinder;
import org.apache.hadoop.util.StringUtils;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.kududb.client.AsyncKuduClient;
import org.kududb.client.ColumnRangePredicate;
import org.kududb.client.KuduTable;
import org.kududb.client.Operation;

import java.io.IOException;
import java.net.URL;
import java.net.URLDecoder;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Utility class to setup MR jobs that use Kudu as an input and/or output.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public class KuduTableMapReduceUtil {
  // Mostly lifted from HBase's TableMapReduceUtil

  private static final Log LOG = LogFactory.getLog(KuduTableMapReduceUtil.class);

  /**
   * Doesn't need instantiation
   */
  private KuduTableMapReduceUtil() { }


  /**
   * Base class for MR I/O formats, contains the common configurations.
   */
  private static abstract class AbstractMapReduceConfigurator<S> {
    protected final Job job;
    protected final String table;

    protected boolean addDependencies = true;

    /**
     * Constructor for the required fields to configure.
     * @param job a job to configure
     * @param table a string that contains the name of the table to read from
     */
    private AbstractMapReduceConfigurator(Job job, String table) {
      this.job = job;
      this.table = table;
    }

    /**
     * Sets whether this job should add Kudu's dependencies to the distributed cache. Turned on
     * by default.
     * @param addDependencies a boolean that says if we should add the dependencies
     * @return this instance
     */
    @SuppressWarnings("unchecked")
    public S addDependencies(boolean addDependencies) {
      this.addDependencies = addDependencies;
      return (S) this;
    }

    /**
     * Configures the job using the passed parameters.
     * @throws IOException If addDependencies is enabled and a problem is encountered reading
     * files on the filesystem
     */
    public abstract void configure() throws IOException;
  }

  /**
   * Builder-like class that sets up the required configurations and classes to write to Kudu.
   * <p>
   * Use either child classes when configuring the table output format.
   */
  private static abstract class AbstractTableOutputFormatConfigurator
      <S extends AbstractTableOutputFormatConfigurator<? super S>>
      extends AbstractMapReduceConfigurator<S> {

    protected String masterAddresses;
    protected long operationTimeoutMs = AsyncKuduClient.DEFAULT_OPERATION_TIMEOUT_MS;

    /**
     * {@inheritDoc}
     */
    private AbstractTableOutputFormatConfigurator(Job job, String table) {
      super(job, table);
    }

    /**
     * {@inheritDoc}
     */
    public void configure() throws IOException {
      job.setOutputFormatClass(KuduTableOutputFormat.class);
      job.setOutputKeyClass(NullWritable.class);
      job.setOutputValueClass(Operation.class);

      Configuration conf = job.getConfiguration();
      conf.set(KuduTableOutputFormat.MASTER_ADDRESSES_KEY, masterAddresses);
      conf.set(KuduTableOutputFormat.OUTPUT_TABLE_KEY, table);
      conf.setLong(KuduTableOutputFormat.OPERATION_TIMEOUT_MS_KEY, operationTimeoutMs);
      if (addDependencies) {
        addDependencyJars(job);
      }
    }
  }

  /**
   * Builder-like class that sets up the required configurations and classes to read from Kudu.
   * By default, block caching is disabled.
   * <p>
   * Use either child classes when configuring the table input format.
   */
  private static abstract class AbstractTableInputFormatConfigurator
      <S extends AbstractTableInputFormatConfigurator<? super S>>
      extends AbstractMapReduceConfigurator<S> {

    protected String masterAddresses;
    protected long operationTimeoutMs = AsyncKuduClient.DEFAULT_OPERATION_TIMEOUT_MS;
    protected final String columnProjection;
    protected boolean cacheBlocks;
    protected List<ColumnRangePredicate> columnRangePredicates = new ArrayList<>();

    /**
     * Constructor for the required fields to configure.
     * @param job a job to configure
     * @param table a string that contains the name of the table to read from
     * @param columnProjection a string containing a comma-separated list of columns to read.
     *                         It can be null in which case we read empty rows
     */
    private AbstractTableInputFormatConfigurator(Job job, String table, String columnProjection) {
      super(job, table);
      this.columnProjection = columnProjection;
    }

    /**
     * Sets the block caching configuration for the scanners. Turned off by default.
     * @param cacheBlocks whether the job should use scanners that cache blocks.
     * @return this instance
     */
    public S cacheBlocks(boolean cacheBlocks) {
      this.cacheBlocks = cacheBlocks;
      return (S) this;
    }

    /**
     * Configures the job with all the passed parameters.
     * @throws IOException If addDependencies is enabled and a problem is encountered reading
     * files on the filesystem
     */
    public void configure() throws IOException {
      job.setInputFormatClass(KuduTableInputFormat.class);

      Configuration conf = job.getConfiguration();

      conf.set(KuduTableInputFormat.MASTER_ADDRESSES_KEY, masterAddresses);
      conf.set(KuduTableInputFormat.INPUT_TABLE_KEY, table);
      conf.setLong(KuduTableInputFormat.OPERATION_TIMEOUT_MS_KEY, operationTimeoutMs);
      conf.setBoolean(KuduTableInputFormat.SCAN_CACHE_BLOCKS, cacheBlocks);

      if (columnProjection != null) {
        conf.set(KuduTableInputFormat.COLUMN_PROJECTION_KEY, columnProjection);
      }

      if (!columnRangePredicates.isEmpty()) {
        conf.set(KuduTableInputFormat.ENCODED_COLUMN_RANGE_PREDICATES_KEY,
            base64EncodePredicates(columnRangePredicates));
      }

      if (addDependencies) {
        addDependencyJars(job);
      }
    }
  }

  static String base64EncodePredicates(List<ColumnRangePredicate> predicates) {
    byte[] predicateBytes = ColumnRangePredicate.toByteArray(predicates);
    return Base64.encodeBase64String(predicateBytes);
  }


  /**
   * Table output format configurator to use to specify the parameters directly.
   */
  public static class TableOutputFormatConfigurator
      extends AbstractTableOutputFormatConfigurator<TableOutputFormatConfigurator> {

    /**
     * Constructor for the required fields to configure.
     * @param job a job to configure
     * @param table a string that contains the name of the table to read from
     * @param masterAddresses a comma-separated list of masters' hosts and ports
     */
    public TableOutputFormatConfigurator(Job job, String table, String masterAddresses) {
      super(job, table);
      this.masterAddresses = masterAddresses;
    }

    /**
     * Sets the timeout for all the operations. The default is 10 seconds.
     * @param operationTimeoutMs a long that represents the timeout for operations to complete,
     *                           must be a positive value or 0
     * @return this instance
     * @throws IllegalArgumentException if the operation timeout is lower than 0
     */
    public TableOutputFormatConfigurator operationTimeoutMs(long operationTimeoutMs) {
      if (operationTimeoutMs < 0) {
        throw new IllegalArgumentException("The operation timeout must be => 0, " +
            "passed value is: " + operationTimeoutMs);
      }
      this.operationTimeoutMs = operationTimeoutMs;
      return this;
    }
  }

  /**
   * Table output format that uses a {@link CommandLineParser} in order to set the
   * master config and the operation timeout.
   */
  public static class TableOutputFormatConfiguratorWithCommandLineParser extends
      AbstractTableOutputFormatConfigurator<TableOutputFormatConfiguratorWithCommandLineParser> {

    /**
     * {@inheritDoc}
     */
    public TableOutputFormatConfiguratorWithCommandLineParser(Job job, String table) {
      super(job, table);
      CommandLineParser parser = new CommandLineParser(job.getConfiguration());
      this.masterAddresses = parser.getMasterAddresses();
      this.operationTimeoutMs = parser.getOperationTimeoutMs();
    }
  }

  /**
   * Table input format configurator to use to specify the parameters directly.
   */
  public static class TableInputFormatConfigurator
      extends AbstractTableInputFormatConfigurator<TableInputFormatConfigurator> {

    /**
     * Constructor for the required fields to configure.
     * @param job a job to configure
     * @param table a string that contains the name of the table to read from
     * @param columnProjection a string containing a comma-separated list of columns to read.
     *                         It can be null in which case we read empty rows
     * @param masterAddresses a comma-separated list of masters' hosts and ports
     */
    public TableInputFormatConfigurator(Job job, String table, String columnProjection,
                                        String masterAddresses) {
      super(job, table, columnProjection);
      this.masterAddresses = masterAddresses;
    }

    /**
     * Sets the timeout for all the operations. The default is 10 seconds.
     * @param operationTimeoutMs a long that represents the timeout for operations to complete,
     *                           must be a positive value or 0
     * @return this instance
     * @throws IllegalArgumentException if the operation timeout is lower than 0
     */
    public TableInputFormatConfigurator operationTimeoutMs(long operationTimeoutMs) {
      if (operationTimeoutMs < 0) {
        throw new IllegalArgumentException("The operation timeout must be => 0, " +
            "passed value is: " + operationTimeoutMs);
      }
      this.operationTimeoutMs = operationTimeoutMs;
      return this;
    }

    /**
     * Adds a new predicate that will be pushed down to all the tablets.
     * @param predicate a predicate to add
     * @return this instance
     */
    public TableInputFormatConfigurator addColumnRangePredicate(ColumnRangePredicate predicate) {
      this.columnRangePredicates.add(predicate);
      return this;
    }
  }

  /**
   * Table input format that uses a {@link CommandLineParser} in order to set the
   * master config and the operation timeout.
   * This version cannot set column range predicates.
   */
  public static class TableInputFormatConfiguratorWithCommandLineParser extends
      AbstractTableInputFormatConfigurator<TableInputFormatConfiguratorWithCommandLineParser> {

    /**
     * {@inheritDoc}
     */
    public TableInputFormatConfiguratorWithCommandLineParser(Job job,
                                                             String table,
                                                             String columnProjection) {
      super(job, table, columnProjection);
      CommandLineParser parser = new CommandLineParser(job.getConfiguration());
      this.masterAddresses = parser.getMasterAddresses();
      this.operationTimeoutMs = parser.getOperationTimeoutMs();
    }
  }

  /**
   * Use this method when setting up a task to get access to the KuduTable in order to create
   * Inserts, Updates, and Deletes.
   * @param context Map context
   * @return The kudu table object as setup by the output format
   */
  @SuppressWarnings("rawtypes")
  public static KuduTable getTableFromContext(TaskInputOutputContext context) {
    String multitonKey = context.getConfiguration().get(KuduTableOutputFormat.MULTITON_KEY);
    return KuduTableOutputFormat.getKuduTable(multitonKey);
  }

  /**
   * Add the Kudu dependency jars as well as jars for any of the configured
   * job classes to the job configuration, so that JobClient will ship them
   * to the cluster and add them to the DistributedCache.
   */
  public static void addDependencyJars(Job job) throws IOException {
    addKuduDependencyJars(job.getConfiguration());
    try {
      addDependencyJars(job.getConfiguration(),
          // when making changes here, consider also mapred.TableMapReduceUtil
          // pull job classes
          job.getMapOutputKeyClass(),
          job.getMapOutputValueClass(),
          job.getInputFormatClass(),
          job.getOutputKeyClass(),
          job.getOutputValueClass(),
          job.getOutputFormatClass(),
          job.getPartitionerClass(),
          job.getCombinerClass());
    } catch (ClassNotFoundException e) {
      throw new IOException(e);
    }
  }

  /**
   * Add the jars containing the given classes to the job's configuration
   * such that JobClient will ship them to the cluster and add them to
   * the DistributedCache.
   */
  public static void addDependencyJars(Configuration conf,
                                       Class<?>... classes) throws IOException {

    FileSystem localFs = FileSystem.getLocal(conf);
    Set<String> jars = new HashSet<String>();
    // Add jars that are already in the tmpjars variable
    jars.addAll(conf.getStringCollection("tmpjars"));

    // add jars as we find them to a map of contents jar name so that we can avoid
    // creating new jars for classes that have already been packaged.
    Map<String, String> packagedClasses = new HashMap<String, String>();

    // Add jars containing the specified classes
    for (Class<?> clazz : classes) {
      if (clazz == null) continue;

      Path path = findOrCreateJar(clazz, localFs, packagedClasses);
      if (path == null) {
        LOG.warn("Could not find jar for class " + clazz +
            " in order to ship it to the cluster.");
        continue;
      }
      if (!localFs.exists(path)) {
        LOG.warn("Could not validate jar file " + path + " for class "
            + clazz);
        continue;
      }
      jars.add(path.toString());
    }
    if (jars.isEmpty()) return;

    conf.set("tmpjars", StringUtils.arrayToString(jars.toArray(new String[jars.size()])));
  }

  /**
   * Add Kudu and its dependencies (only) to the job configuration.
   * <p>
   * This is intended as a low-level API, facilitating code reuse between this
   * class and its mapred counterpart. It also of use to external tools that
   * need to build a MapReduce job that interacts with Kudu but want
   * fine-grained control over the jars shipped to the cluster.
   * </p>
   * @param conf The Configuration object to extend with dependencies.
   * @see KuduTableMapReduceUtil
   * @see <a href="https://issues.apache.org/jira/browse/PIG-3285">PIG-3285</a>
   */
  public static void addKuduDependencyJars(Configuration conf) throws IOException {
    addDependencyJars(conf,
        // explicitly pull a class from each module
        Operation.class,                      // kudu-client
        KuduTableMapReduceUtil.class,   // kudu-mapreduce
        // pull necessary dependencies
        com.stumbleupon.async.Deferred.class);
  }

  /**
   * If org.apache.hadoop.util.JarFinder is available (0.23+ hadoop), finds
   * the Jar for a class or creates it if it doesn't exist. If the class is in
   * a directory in the classpath, it creates a Jar on the fly with the
   * contents of the directory and returns the path to that Jar. If a Jar is
   * created, it is created in the system temporary directory. Otherwise,
   * returns an existing jar that contains a class of the same name. Maintains
   * a mapping from jar contents to the tmp jar created.
   * @param my_class the class to find.
   * @param fs the FileSystem with which to qualify the returned path.
   * @param packagedClasses a map of class name to path.
   * @return a jar file that contains the class.
   * @throws IOException
   */
  @SuppressWarnings("deprecation")
  private static Path findOrCreateJar(Class<?> my_class, FileSystem fs,
                                      Map<String, String> packagedClasses)
      throws IOException {
    // attempt to locate an existing jar for the class.
    String jar = findContainingJar(my_class, packagedClasses);
    if (null == jar || jar.isEmpty()) {
      jar = JarFinder.getJar(my_class);
      updateMap(jar, packagedClasses);
    }

    if (null == jar || jar.isEmpty()) {
      return null;
    }

    LOG.debug(String.format("For class %s, using jar %s", my_class.getName(), jar));
    return new Path(jar).makeQualified(fs);
  }

  /**
   * Find a jar that contains a class of the same name, if any. It will return
   * a jar file, even if that is not the first thing on the class path that
   * has a class with the same name. Looks first on the classpath and then in
   * the <code>packagedClasses</code> map.
   * @param my_class the class to find.
   * @return a jar file that contains the class, or null.
   * @throws IOException
   */
  private static String findContainingJar(Class<?> my_class, Map<String, String> packagedClasses)
      throws IOException {
    ClassLoader loader = my_class.getClassLoader();
    String class_file = my_class.getName().replaceAll("\\.", "/") + ".class";

    // first search the classpath
    for (Enumeration<URL> itr = loader.getResources(class_file); itr.hasMoreElements();) {
      URL url = itr.nextElement();
      if ("jar".equals(url.getProtocol())) {
        String toReturn = url.getPath();
        if (toReturn.startsWith("file:")) {
          toReturn = toReturn.substring("file:".length());
        }
        // URLDecoder is a misnamed class, since it actually decodes
        // x-www-form-urlencoded MIME type rather than actual
        // URL encoding (which the file path has). Therefore it would
        // decode +s to ' 's which is incorrect (spaces are actually
        // either unencoded or encoded as "%20"). Replace +s first, so
        // that they are kept sacred during the decoding process.
        toReturn = toReturn.replaceAll("\\+", "%2B");
        toReturn = URLDecoder.decode(toReturn, "UTF-8");
        return toReturn.replaceAll("!.*$", "");
      }
    }

    // now look in any jars we've packaged using JarFinder. Returns null when
    // no jar is found.
    return packagedClasses.get(class_file);
  }

  /**
   * Add entries to <code>packagedClasses</code> corresponding to class files
   * contained in <code>jar</code>.
   * @param jar The jar who's content to list.
   * @param packagedClasses map[class -> jar]
   */
  private static void updateMap(String jar, Map<String, String> packagedClasses) throws IOException {
    if (null == jar || jar.isEmpty()) {
      return;
    }
    ZipFile zip = null;
    try {
      zip = new ZipFile(jar);
      for (Enumeration<? extends ZipEntry> iter = zip.entries(); iter.hasMoreElements();) {
        ZipEntry entry = iter.nextElement();
        if (entry.getName().endsWith("class")) {
          packagedClasses.put(entry.getName(), jar);
        }
      }
    } finally {
      if (null != zip) zip.close();
    }
  }
}
