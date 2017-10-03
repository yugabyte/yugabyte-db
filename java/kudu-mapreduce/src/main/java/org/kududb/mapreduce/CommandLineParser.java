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
import org.kududb.client.AsyncKuduClient;
import org.apache.hadoop.conf.Configuration;
import org.kududb.client.KuduClient;

/**
 * Utility class that manages common configurations to all MR jobs. For example,
 * any job that uses {#KuduTableMapReduceUtil} to setup an input or output format
 * and that has parsed the command line arguments with
 * {@link org.apache.hadoop.util.GenericOptionsParser} can simply be passed:
 * <code>
 * -Dmaster.address=ADDRESS
 * </code>
 * in order to specify where the master is.
 * Use {@link CommandLineParser#getHelpSnippet()} to provide usage text for the configurations
 * managed by this class.
 */
@InterfaceAudience.Public
@InterfaceStability.Unstable
public class CommandLineParser {
  private final Configuration conf;
  public static final String MASTER_ADDRESSES_KEY = "kudu.master.addresses";
  public static final String MASTER_ADDRESSES_DEFAULT = "127.0.0.1";
  public static final String OPERATION_TIMEOUT_MS_KEY = "kudu.operation.timeout.ms";
  public static final long OPERATION_TIMEOUT_MS_DEFAULT =
      AsyncKuduClient.DEFAULT_OPERATION_TIMEOUT_MS;
  public static final String ADMIN_OPERATION_TIMEOUT_MS_KEY = "kudu.admin.operation.timeout.ms";
  public static final String SOCKET_READ_TIMEOUT_MS_KEY = "kudu.socket.read.timeout.ms";
  public static final long SOCKET_READ_TIMEOUT_MS_DEFAULT =
      AsyncKuduClient.DEFAULT_SOCKET_READ_TIMEOUT_MS;
  public static final String NUM_REPLICAS_KEY = "kudu.num.replicas";
  public static final int NUM_REPLICAS_DEFAULT = 3;

  /**
   * Constructor that uses a Configuration that has already been through
   * {@link org.apache.hadoop.util.GenericOptionsParser}'s command line parsing.
   * @param conf the configuration from which job configurations will be extracted
   */
  public CommandLineParser(Configuration conf) {
    this.conf = conf;
  }

  /**
   * Get the configured master's config.
   * @return a string that contains the passed config, or the default value
   */
  public String getMasterAddresses() {
    return conf.get(MASTER_ADDRESSES_KEY, MASTER_ADDRESSES_DEFAULT);
  }

  /**
   * Get the configured timeout for operations on sessions and scanners.
   * @return a long that represents the passed timeout, or the default value
   */
  public long getOperationTimeoutMs() {
    return conf.getLong(OPERATION_TIMEOUT_MS_KEY, OPERATION_TIMEOUT_MS_DEFAULT);
  }

  /**
   * Get the configured timeout for admin operations.
   * @return a long that represents the passed timeout, or the default value
   */
  public long getAdminOperationTimeoutMs() {
    return conf.getLong(ADMIN_OPERATION_TIMEOUT_MS_KEY, OPERATION_TIMEOUT_MS_DEFAULT);
  }

  /**
   * Get the configured timeout for socket reads.
   * @return a long that represents the passed timeout, or the default value
   */
  public long getSocketReadTimeoutMs() {
    return conf.getLong(SOCKET_READ_TIMEOUT_MS_KEY, SOCKET_READ_TIMEOUT_MS_DEFAULT);
  }

  /**
   * Get the number of replicas to use when configuring a new table.
   * @return an int that represents the passed number of replicas to use, or the default value.
   */
  public int getNumReplicas() {
    return conf.getInt(NUM_REPLICAS_KEY, NUM_REPLICAS_DEFAULT);
  }

  /**
   * Get an async client connected to the configured Master(s).
   * @return an async kudu client
   */
  public AsyncKuduClient getAsyncClient() {
    return new AsyncKuduClient.AsyncKuduClientBuilder(getMasterAddresses())
        .defaultOperationTimeoutMs(getOperationTimeoutMs())
        .defaultAdminOperationTimeoutMs(getAdminOperationTimeoutMs())
        .defaultSocketReadTimeoutMs(getSocketReadTimeoutMs())
        .build();
  }

  /**
   * Get a client connected to the configured Master(s).
   * @return a kudu client
   */
  public KuduClient getClient() {
    return new KuduClient.KuduClientBuilder(getMasterAddresses())
        .defaultOperationTimeoutMs(getOperationTimeoutMs())
        .defaultAdminOperationTimeoutMs(getAdminOperationTimeoutMs())
        .defaultSocketReadTimeoutMs(getSocketReadTimeoutMs())
        .build();
  }

  /**
   * This method returns a single multi-line string that contains the help snippet to append to
   * the tail of a usage() or help() type of method.
   * @return a string with all the available configurations and their defaults
   */
  public static String getHelpSnippet() {
    return "\nAdditionally, the following options are available:" +
      "  -D" + OPERATION_TIMEOUT_MS_KEY + "=TIME - timeout for read and write " +
          "operations, defaults to " + OPERATION_TIMEOUT_MS_DEFAULT + " \n"+
      "  -D" + ADMIN_OPERATION_TIMEOUT_MS_KEY + "=TIME - timeout for admin operations " +
        ", defaults to " + OPERATION_TIMEOUT_MS_DEFAULT + " \n"+
      "  -D" + SOCKET_READ_TIMEOUT_MS_KEY + "=TIME - timeout for socket reads " +
        ", defaults to " + SOCKET_READ_TIMEOUT_MS_DEFAULT + " \n"+
      "  -D" + MASTER_ADDRESSES_KEY + "=ADDRESSES - addresses to reach the Masters, " +
        "defaults to " + MASTER_ADDRESSES_DEFAULT + " which is usually wrong.\n" +
      "  -D " + NUM_REPLICAS_KEY + "=NUM - number of replicas to use when configuring a new " +
        "table, defaults to " + NUM_REPLICAS_DEFAULT;
  }
}
