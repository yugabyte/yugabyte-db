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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
package org.yb.client;

import static org.yb.CommonTypes.TableType;

import org.yb.Schema;
import org.yb.annotations.InterfaceAudience;
import org.yb.annotations.InterfaceStability;

import com.stumbleupon.async.Deferred;

import java.util.List;
import java.util.Map;
import java.util.HashMap;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;


/**
 * A YBTable represents a table on a particular cluster. It holds the current
 * schema of the table. Any given YBTable instance belongs to a specific AsyncYBClient
 * instance.
 *
 * Upon construction, the table is looked up in the catalog (or catalog cache),
 * and the schema fetched for introspection. The schema is not kept in sync with the master.
 *
 * This class is thread-safe.
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public class YBTable {

  private final Schema schema;
  private final PartitionSchema partitionSchema;
  private final AsyncYBClient client;
  private final String name;
  private final String keyspace;
  private final String tableId;
  private final TableType tableType;

  private final boolean colocated;

  private static final String OBSERVER = "OBSERVER";
  private static final String PRE_OBSERVER = "PRE_OBSERVER";

  /**
   * Package-private constructor, use {@link YBClient#openTable(String)} to get an instance.
   * @param client the client this instance belongs to
   * @param name this table's name
   * @param schema this table's schema
   */
  YBTable(AsyncYBClient client, String name, String tableId, Schema schema,
          PartitionSchema partitionSchema, TableType tableType, String keyspace,
          boolean colocated) {
    this.schema = schema;
    this.partitionSchema = partitionSchema;
    this.client = client;
    this.name = name;
    this.tableId = tableId;
    this.tableType = tableType;
    this.keyspace = keyspace;
    this.colocated = colocated;
  }

  YBTable(AsyncYBClient client, String name, String tableId,
          Schema schema, PartitionSchema partitionSchema, boolean colocated) {
    this(client, name, tableId, schema, partitionSchema, TableType.YQL_TABLE_TYPE,
         null, colocated);
  }

  /**
   * Get this table's schema, as of the moment this instance was created.
   * @return this table's schema
   */
  public Schema getSchema() {
    return this.schema;
  }

  /**
   * Get this table's type.
   */
  public TableType getTableType() {
    return this.tableType;
  }

  /**
   * Gets the table's partition schema.
   *
   * This method is new, and not considered stable or suitable for public use.
   *
   * @return the table's partition schema.
   */
  @InterfaceAudience.LimitedPrivate("Impala")
  @InterfaceStability.Unstable
  public PartitionSchema getPartitionSchema() {
    return partitionSchema;
  }

  /**
   * Get this table's name.
   * @return this table's name
   */
  public String getName() {
    return this.name;
  }

  /**
   * Get whether the table is colocated.
   * @return true if table is colocated, false otherwise.
   */
  public boolean isColocated() {
    return this.colocated;
  }

  /**
   * Get this table's keyspace.
   * @return this table's keyspace.
   */
  public String getKeyspace() {
    return this.keyspace;
  }

  /**
   * Get this table's unique identifier.
   * @return this table's tableId
   */
  public String getTableId() {
    return tableId;
  }

  /**
   * Get the async client that created this instance.
   * @return an async yb java client.
   */
  public AsyncYBClient getAsyncClient() {
    return this.client;
  }

  /**
   * Get all the tablets for this table. This may query the master multiple times if there
   * are a lot of tablets.
   * @param deadline deadline in milliseconds for this method to finish
   * @return a list containing the metadata and locations for each of the tablets in the
   *         table
   * @throws Exception
   */
  public List<LocatedTablet> getTabletsLocations(
      long deadline) throws Exception {
    return getTabletsLocations(null, null, deadline);
  }

  /**
   * Asynchronously get all the tablets for this table.
   * @param deadline max time spent in milliseconds for the deferred result of this method to
   *         get called back, if deadline is reached, the deferred result will get erred back
   * @return a {@link Deferred} object that yields a list containing the metadata and
   * locations for each of the tablets in the table
   */
  public Deferred<List<LocatedTablet>> asyncGetTabletsLocations(
      long deadline) throws Exception {
    return asyncGetTabletsLocations(null, null, deadline);
  }

  /**
   * Get all or some tablets for this table. This may query the master multiple times if there
   * are a lot of tablets.
   * This method blocks until it gets all the tablets.
   * @param startKey where to start in the table, pass null to start at the beginning
   * @param endKey where to stop in the table, pass null to get all the tablets until the end of
   *               the table
   * @param deadline deadline in milliseconds for this method to finish
   * @return a list containing the metadata and locations for each of the tablets in the
   *         table
   * @throws Exception
   */
  public List<LocatedTablet> getTabletsLocations(
      byte[] startKey, byte[] endKey, long deadline) throws Exception {
    return client.syncLocateTable(tableId, startKey, endKey, deadline);
  }

  /**
   * Asynchronously get all or some tablets for this table.
   * @param startKey where to start in the table, pass null to start at the beginning
   * @param endKey where to stop in the table, pass null to get all the tablets until the end of
   *               the table
   * @param deadline max time spent in milliseconds for the deferred result of this method to
   *         get called back, if deadline is reached, the deferred result will get erred back
   * @return a {@link Deferred} object that yields a list containing the metadata and locations
   *           for each of the tablets in the table
   */
  public Deferred<List<LocatedTablet>> asyncGetTabletsLocations(
      byte[] startKey, byte[] endKey, long deadline) throws Exception {
    return client.locateTable(tableId, startKey, endKey, deadline);
  }

  /**
   * Loop through all replicas in the table and store a mapping from tserver placement uuid to
   * a list of lists, containing the live replica count per ts, followed by the read
   * replica count per ts. If there are two placement uuids, live and readOnly, and two
   * tservers in each uuid, and RF=1 for both live and readOnly with 8 tablets
   * in the table, the resulting map would look like this:
   * "live" : { [[1, 1], [0, 0]] }, "readOnly" : { [[0, 0], [1, 1]] }.
   * The first list in the map correspond to live replica counts per tserver, and the
   * second list corresponds to read only replica counts.
   * @param deadline deadline in milliseconds for getTabletsLocations rpc.
   * @return a map from placement zone to a list of lists of integers.
   */
  public Map<String, List<List<Integer>>> getMemberTypeCountsForEachTSType(long deadline)
      throws Exception {
    // Intermediate map which contains an internal map from ts uuid to live and
    // read replica counts.
    Map<String, Map<String, List<Integer>>> intermediateMap =
        new HashMap<String, Map<String, List<Integer>>>();
    List<LocatedTablet> tablets = getTabletsLocations(deadline);
    for (LocatedTablet tablet : tablets) {
      for (LocatedTablet.Replica replica : tablet.getReplicas()) {
        String placementUuid = replica.getTsPlacementUuid();
        Map<String, List<Integer>> tsMap;
        if (intermediateMap.containsKey(placementUuid)) {
          tsMap = intermediateMap.get(placementUuid);
        } else {
          tsMap = new HashMap<String, List<Integer>>();
        }
        String tsUuid = replica.getTsUuid();
        List<Integer> liveReadOnlyCounts;
        if (tsMap.containsKey(tsUuid)) {
          liveReadOnlyCounts = tsMap.get(tsUuid);
        } else {
          liveReadOnlyCounts = new ArrayList<>(Arrays.asList(0, 0));
        }
        if (replica.getMemberType().equals(OBSERVER) ||
            replica.getMemberType().equals(PRE_OBSERVER)) {
          // This is an read only member,
          int currCount = liveReadOnlyCounts.get(1);
          liveReadOnlyCounts.set(1, currCount + 1);
        } else {
          int currCount = liveReadOnlyCounts.get(0);
          liveReadOnlyCounts.set(0, currCount + 1);
        }
        tsMap.put(tsUuid, liveReadOnlyCounts);
        intermediateMap.put(placementUuid, tsMap);
      }
    }

    // Now, convert our internal map into the return map by getting rid of ts uuid.
    Map<String, List<List<Integer>>> returnMap = new HashMap<String, List<List<Integer>>>();
    for (Map.Entry<String, Map<String, List<Integer>>> placementEntry :
         intermediateMap.entrySet()) {
      List<List<Integer>> newEntry = new ArrayList<List<Integer>>();
      List<Integer> liveCounts = new ArrayList<Integer>();
      List<Integer> readOnlyCounts = new ArrayList<Integer>();
      for (Map.Entry<String, List<Integer>> tsEntry : placementEntry.getValue().entrySet()) {
        liveCounts.add(tsEntry.getValue().get(0));
        readOnlyCounts.add(tsEntry.getValue().get(1));
      }
      Collections.sort(liveCounts);
      Collections.sort(readOnlyCounts);
      newEntry.add(liveCounts);
      newEntry.add(readOnlyCounts);
      returnMap.put(placementEntry.getKey(), newEntry);;
    }
    return returnMap;
  }
}
