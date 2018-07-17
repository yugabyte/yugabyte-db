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

package com.yugabyte.sample.apps;

import java.util.Arrays;
import java.util.List;

import org.apache.log4j.Logger;

/**
 * This workload writes and reads some random string keys from a YCQL table with a unique secondary
 * index on a non-primary-key column. When it reads a key, it queries the key by its associated
 * value
 * which is indexed.
 */
public class CassandraUniqueSecondaryIndex extends CassandraSecondaryIndex {
  private static final Logger LOG = Logger.getLogger(CassandraSecondaryIndex.class);

  // The default table name to create and use for CRUD ops.
  private static final String DEFAULT_TABLE_NAME =
    CassandraUniqueSecondaryIndex.class.getSimpleName();

  public CassandraUniqueSecondaryIndex() {
  }

  @Override
  public List<String> getCreateTableStatements() {
    return Arrays.asList(
      String.format(
        "CREATE TABLE IF NOT EXISTS %s (k varchar, v varchar, PRIMARY KEY (k)) " +
        "WITH transactions = { 'enabled' : true };", getTableName()),
      String.format(
        "CREATE UNIQUE INDEX IF NOT EXISTS %sByValue ON %s (v);", getTableName(), getTableName()));
  }

  @Override
  public String getTableName() {
    return appConfig.tableName != null ? appConfig.tableName : DEFAULT_TABLE_NAME;
  }

  @Override
  public List<String> getWorkloadDescription() {
    return Arrays.asList(
      "Sample key-value app built on Cassandra. The app writes out unique string keys",
      "each with a string value to a YCQL table with a unique index on the value column.",
      "There are multiple readers and writers that update these keys and read them",
      "indefinitely, with the readers query the keys by the associated values that are",
      "indexed. Note that the number of reads and writes to perform can be specified as",
      "a parameter.");
  }

  @Override
  public List<String> getExampleUsageOptions() {
    return Arrays.asList(
      "--num_unique_keys " + appConfig.numUniqueKeysToWrite,
      "--num_reads " + appConfig.numKeysToRead,
      "--num_writes " + appConfig.numKeysToWrite,
      "--num_threads_read " + appConfig.numReaderThreads,
      "--num_threads_write " + appConfig.numWriterThreads);
  }
}
