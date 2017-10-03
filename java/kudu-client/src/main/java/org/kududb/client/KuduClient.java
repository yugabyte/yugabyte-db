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
import org.kududb.Schema;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executor;

/**
 * A synchronous and thread-safe client for Kudu.
 * <p>
 * This class acts as a wrapper around {@link AsyncKuduClient}. The {@link Deferred} objects are
 * joined against using the default admin operation timeout
 * (see {@link org.kududb.client.KuduClient.KuduClientBuilder#defaultAdminOperationTimeoutMs(long)} (long)}).
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public class KuduClient implements AutoCloseable {

  public static final Logger LOG = LoggerFactory.getLogger(AsyncKuduClient.class);

  private final AsyncKuduClient asyncClient;

  KuduClient(AsyncKuduClient asyncClient) {
    this.asyncClient = asyncClient;
  }

  /**
   * Create a table on the cluster with the specified name and schema. Default table
   * configurations are used, mainly the table will have one tablet.
   * @param name Table's name
   * @param schema Table's schema
   * @return an object to communicate with the created table
   */
  public KuduTable createTable(String name, Schema schema) throws Exception {
    return createTable(name, schema, new CreateTableOptions());
  }

  /**
   * Create a table on the cluster with the specified name, schema, and table configurations.
   * @param name the table's name
   * @param schema the table's schema
   * @param builder a builder containing the table's configurations
   * @return an object to communicate with the created table
   */
  public KuduTable createTable(String name, Schema schema, CreateTableOptions builder)
      throws Exception {
    Deferred<KuduTable> d = asyncClient.createTable(name, schema, builder);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Delete a table on the cluster with the specified name.
   * @param name the table's name
   * @return an rpc response object
   */
  public DeleteTableResponse deleteTable(String name) throws Exception {
    Deferred<DeleteTableResponse> d = asyncClient.deleteTable(name);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Alter a table on the cluster as specified by the builder.
   *
   * When the method returns it only indicates that the master accepted the alter
   * command, use {@link KuduClient#isAlterTableDone(String)} to know when the alter finishes.
   * @param name the table's name, if this is a table rename then the old table name must be passed
   * @param ato the alter table builder
   * @return an rpc response object
   */
  public AlterTableResponse alterTable(String name, AlterTableOptions ato) throws Exception {
    Deferred<AlterTableResponse> d = asyncClient.alterTable(name, ato);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Helper method that checks and waits until the completion of an alter command.
   * It will block until the alter command is done or the timeout is reached.
   * @param name Table's name, if the table was renamed then that name must be checked against
   * @return a boolean indicating if the table is done being altered
   */
  public boolean isAlterTableDone(String name) throws Exception {
    long totalSleepTime = 0;
    while (totalSleepTime < getDefaultAdminOperationTimeoutMs()) {
      long start = System.currentTimeMillis();

      Deferred<IsAlterTableDoneResponse> d = asyncClient.isAlterTableDone(name);
      IsAlterTableDoneResponse response;
      try {
        response = d.join(AsyncKuduClient.SLEEP_TIME);
      } catch (Exception ex) {
        throw ex;
      }

      if (response.isDone()) {
        return true;
      }

      // Count time that was slept and see if we need to wait a little more.
      long elapsed = System.currentTimeMillis() - start;
      // Don't oversleep the deadline.
      if (totalSleepTime + AsyncKuduClient.SLEEP_TIME > getDefaultAdminOperationTimeoutMs()) {
        return false;
      }
      // elapsed can be bigger if we slept about 500ms
      if (elapsed <= AsyncKuduClient.SLEEP_TIME) {
        LOG.debug("Alter not done, sleep " + (AsyncKuduClient.SLEEP_TIME - elapsed) +
            " and slept " + totalSleepTime);
        Thread.sleep(AsyncKuduClient.SLEEP_TIME - elapsed);
        totalSleepTime += AsyncKuduClient.SLEEP_TIME;
      } else {
        totalSleepTime += elapsed;
      }
    }
    return false;
  }

  /**
   * Get the list of running tablet servers.
   * @return a list of tablet servers
   */
  public ListTabletServersResponse listTabletServers() throws Exception {
    Deferred<ListTabletServersResponse> d = asyncClient.listTabletServers();
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get the list of all the tables.
   * @return a list of all the tables
   */
  public ListTablesResponse getTablesList() throws Exception {
    return getTablesList(null);
  }

  /**
   * Get a list of table names. Passing a null filter returns all the tables. When a filter is
   * specified, it only returns tables that satisfy a substring match.
   * @param nameFilter an optional table name filter
   * @return a deferred that contains the list of table names
   */
  public ListTablesResponse getTablesList(String nameFilter) throws Exception {
    Deferred<ListTablesResponse> d = asyncClient.getTablesList(nameFilter);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Test if a table exists.
   * @param name a non-null table name
   * @return true if the table exists, else false
   */
  public boolean tableExists(String name) throws Exception {
    Deferred<Boolean> d = asyncClient.tableExists(name);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Open the table with the given name. If the table was just created, this method will block until
   * all its tablets have also been created.
   * @param name table to open
   * @return a KuduTable if the table exists, else a MasterErrorException
   */
  public KuduTable openTable(final String name) throws Exception {
    Deferred<KuduTable> d = asyncClient.openTable(name);
    return d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Create a new session for interacting with the cluster.
   * User is responsible for destroying the session object.
   * This is a fully local operation (no RPCs or blocking).
   * @return a synchronous wrapper around KuduSession.
   */
  public KuduSession newSession() {
    AsyncKuduSession session = asyncClient.newSession();
    return new KuduSession(session);
  }

  /**
   * Creates a new {@link KuduScanner.KuduScannerBuilder} for a particular table.
   * @param table the name of the table you intend to scan.
   * The string is assumed to use the platform's default charset.
   * @return a new scanner builder for this table
   */
  public KuduScanner.KuduScannerBuilder newScannerBuilder(KuduTable table) {
    return new KuduScanner.KuduScannerBuilder(asyncClient, table);
  }

  /**
   * Analogous to {@link #shutdown()}.
   * @throws Exception if an error happens while closing the connections
   */
  @Override
  public void close() throws Exception {
    asyncClient.close();
  }

  /**
   * Performs a graceful shutdown of this instance.
   * @throws Exception
   */
  public void shutdown() throws Exception {
    Deferred<ArrayList<Void>> d = asyncClient.shutdown();
    d.join(getDefaultAdminOperationTimeoutMs());
  }

  /**
   * Get the timeout used for operations on sessions and scanners.
   * @return a timeout in milliseconds
   */
  public long getDefaultOperationTimeoutMs() {
    return asyncClient.getDefaultOperationTimeoutMs();
  }

  /**
   * Get the timeout used for admin operations.
   * @return a timeout in milliseconds
   */
  public long getDefaultAdminOperationTimeoutMs() {
    return asyncClient.getDefaultAdminOperationTimeoutMs();
  }

  /**
   * Builder class to use in order to connect to Kudu.
   * All the parameters beyond those in the constructors are optional.
   */
  public final static class KuduClientBuilder {
    private AsyncKuduClient.AsyncKuduClientBuilder clientBuilder;

    /**
     * Creates a new builder for a client that will connect to the specified masters.
     * @param masterAddresses comma-separated list of "host:port" pairs of the masters
     */
    public KuduClientBuilder(String masterAddresses) {
      clientBuilder = new AsyncKuduClient.AsyncKuduClientBuilder(masterAddresses);
    }

    /**
     * Creates a new builder for a client that will connect to the specified masters.
     *
     * <p>Here are some examples of recognized formats:
     * <ul>
     *   <li>example.com
     *   <li>example.com:80
     *   <li>192.0.2.1
     *   <li>192.0.2.1:80
     *   <li>[2001:db8::1]
     *   <li>[2001:db8::1]:80
     *   <li>2001:db8::1
     * </ul>
     *
     * @param masterAddresses list of master addresses
     */
    public KuduClientBuilder(List<String> masterAddresses) {
      clientBuilder = new AsyncKuduClient.AsyncKuduClientBuilder(masterAddresses);
    }

    /**
     * Sets the default timeout used for administrative operations (e.g. createTable, deleteTable,
     * etc).
     * Optional.
     * If not provided, defaults to 10s.
     * A value of 0 disables the timeout.
     * @param timeoutMs a timeout in milliseconds
     * @return this builder
     */
    public KuduClientBuilder defaultAdminOperationTimeoutMs(long timeoutMs) {
      clientBuilder.defaultAdminOperationTimeoutMs(timeoutMs);
      return this;
    }

    /**
     * Sets the default timeout used for user operations (using sessions and scanners).
     * Optional.
     * If not provided, defaults to 10s.
     * A value of 0 disables the timeout.
     * @param timeoutMs a timeout in milliseconds
     * @return this builder
     */
    public KuduClientBuilder defaultOperationTimeoutMs(long timeoutMs) {
      clientBuilder.defaultOperationTimeoutMs(timeoutMs);
      return this;
    }

    /**
     * Sets the default timeout to use when waiting on data from a socket.
     * Optional.
     * If not provided, defaults to 5s.
     * A value of 0 disables the timeout.
     * @param timeoutMs a timeout in milliseconds
     * @return this builder
     */
    public KuduClientBuilder defaultSocketReadTimeoutMs(long timeoutMs) {
      clientBuilder.defaultSocketReadTimeoutMs(timeoutMs);
      return this;
    }

    /**
     * Set the executors which will be used for the embedded Netty boss and workers.
     * Optional.
     * If not provided, uses a simple cached threadpool. If either argument is null,
     * then such a thread pool will be used in place of that argument.
     * Note: executor's max thread number must be greater or equal to corresponding
     * worker count, or netty cannot start enough threads, and client will get stuck.
     * If not sure, please just use CachedThreadPool.
     */
    public KuduClientBuilder nioExecutors(Executor bossExecutor, Executor workerExecutor) {
      clientBuilder.nioExecutors(bossExecutor, workerExecutor);
      return this;
    }

    /**
     * Set the maximum number of boss threads.
     * Optional.
     * If not provided, 1 is used.
     */
    public KuduClientBuilder bossCount(int bossCount) {
      clientBuilder.bossCount(bossCount);
      return this;
    }

    /**
     * Set the maximum number of worker threads.
     * Optional.
     * If not provided, (2 * the number of available processors) is used.
     */
    public KuduClientBuilder workerCount(int workerCount) {
      clientBuilder.workerCount(workerCount);
      return this;
    }

    /**
     * Creates a new client that connects to the masters.
     * Doesn't block and won't throw an exception if the masters don't exist.
     * @return a new asynchronous Kudu client
     */
    public KuduClient build() {
      AsyncKuduClient client = clientBuilder.build();
      return new KuduClient(client);
    }

  }
}
