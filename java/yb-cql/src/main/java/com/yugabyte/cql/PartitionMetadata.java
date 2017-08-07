// Copyright (c) YugaByte, Inc.
package com.yugabyte.cql;

import com.datastax.driver.core.AggregateMetadata;
import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.FunctionMetadata;
import com.datastax.driver.core.Host;
import com.datastax.driver.core.KeyspaceMetadata;
import com.datastax.driver.core.MaterializedViewMetadata;
import com.datastax.driver.core.Metadata;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.SchemaChangeListener;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.TableMetadata;
import com.datastax.driver.core.UserType;

import com.google.common.util.concurrent.Futures;
import com.google.common.util.concurrent.FutureCallback;
import com.google.common.util.concurrent.ListenableFuture;

import org.apache.log4j.Logger;

import java.net.InetAddress;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.NavigableMap;
import java.util.Set;
import java.util.TreeMap;
import java.util.Vector;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * The table partition metadata cache of all tables in a cluster. For each table, it tracks the
 * start-key to tablet leader/followers mapping of all partitions of the table. The whole cache is
 * refreshed at regular intervals or triggered when a node or table is added or removed.
 */
class PartitionMetadata implements Host.StateListener, SchemaChangeListener {

  // Query to load partition metadata for all tables.
  public static final String PARTITIONS_QUERY =
      "select keyspace_name, table_name, start_key, replica_addresses from system.partitions;";

  // Frequency in which partition metadata is refreshed.
  private static final int REFRESH_FREQUENCY_SECONDS = 60;

  private static final Logger LOG = Logger.getLogger(PartitionMetadata.class);

  // The session for loading partition metadata.
  private volatile Session session;

  // The table partition map.
  private volatile Map<TableMetadata, NavigableMap<Integer, List<Host>>> tableMap;

  // The future to track the scheduled refresh.
  private volatile ScheduledFuture<?> loadFuture;

  // The cluster medatata.
  private volatile Metadata clusterMetadata;

  // The set of hosts in the cluster and those that are up.
  private final Set<Host> allHosts;
  private final Set<Host> upHosts;

  // Refresh frequency in seconds.
  private final int refreshFrequencySeconds;

  // Load count (for testing purpose only).
  final AtomicInteger loadCount;

  // Scheduler to refresh partition metadata shared across all instances of PartitionMetadata.
  private static final ScheduledExecutorService scheduler = Executors.newScheduledThreadPool(1);

  /**
   * Creates a new {@code PartitionMetadata}.
   *
   * @param cluster  the cluster
   */
  public PartitionMetadata(Cluster cluster) {
    this(cluster, REFRESH_FREQUENCY_SECONDS);
  }

  /**
   * Creates a new {@code PartitionMetadata}.
   *
   * @param cluster                  the cluster
   * @param refreshFrequencySeconds  the refresh frequency in seconds
   */
  public PartitionMetadata(Cluster cluster, int refreshFrequencySeconds) {
    this.allHosts = new HashSet<>();
    this.upHosts = new HashSet<>();
    this.refreshFrequencySeconds = refreshFrequencySeconds;
    this.loadCount = new AtomicInteger();
    cluster.register((Host.StateListener)this);
    cluster.register((SchemaChangeListener)this);
  }

  /**
   * Extracts an unsigned 16-bit number from a {@code ByteBuffer}.
   */
  private static int getKey(ByteBuffer bb) {
    int key = (bb.remaining() == 0) ? 0 : bb.getShort();
    // Flip the negative values back to positive.
    return (key >= 0) ? key : key + 0x10000;
  }

  /**
   * Loads the table partition metadata.
   */
  private void loadAsync() {
    Futures.addCallback(session.executeAsync(PARTITIONS_QUERY),
                        new FutureCallback<ResultSet>() {
                          public void onSuccess(ResultSet rs) {
                            load(rs);
                          }
                          public void onFailure(Throwable t) {
                            LOG.error("execute failed", t);
                          }
                        });
  }

  /**
   * Loads the table partition metadata from the result set.
   */
  private void load(ResultSet rs) {
    Map<TableMetadata, NavigableMap<Integer, List<Host>>> tableMap = new HashMap<>();
    for (Row row : rs) {
      KeyspaceMetadata keyspace = clusterMetadata.getKeyspace(row.getString("keyspace_name"));
      if (keyspace == null) {
        LOG.debug("Keyspace " + row.getString("keyspace_name") + " not found in cluster metadata");
        continue;
      }
      TableMetadata table = keyspace.getTable(row.getString("table_name"));
      if (table == null) {
        LOG.debug("Table " + row.getString("table_name") + " not found in " + keyspace.getName() +
                  " keyspace metadata");
        continue;
      }
      NavigableMap<Integer, List<Host>> partitionMap = tableMap.get(table);
      if (partitionMap == null) {
        partitionMap = new TreeMap<>();
        tableMap.put(table, partitionMap);
      }

      Map<InetAddress, String> replicaAddresses = row.getMap("replica_addresses",
                                                             InetAddress.class,
                                                             String.class);

      // Prepare the host map to look up host by the inet address.
      Map<InetAddress, Host> hostMap = new HashMap<>();
      for (Host host : clusterMetadata.getAllHosts()) {
        hostMap.put(host.getAddress(), host);
      }

      List<Host> hosts = new Vector<>();
      for (Map.Entry<InetAddress, String> entry : replicaAddresses.entrySet()) {
        Host host = hostMap.get(entry.getKey());
        if (host == null) {
          LOG.debug("Host " + entry.getKey() + " not found in cluster metadata for table " +
                    keyspace.getName() + "." + table.getName());
          continue;
        }
        // Put the leader at the beginning and the rest after.
        String role = entry.getValue();
        if (role.equals("LEADER")) {
          hosts.add(0, host);
        } else if (role.equals("FOLLOWER")) {
          hosts.add(host);
        }
      }
      int startKey = getKey(row.getBytes("start_key"));
      partitionMap.put(startKey, hosts);
    }

    // Make the result available.
    this.tableMap = tableMap;
    loadCount.incrementAndGet();

    scheduleNextLoad();
  }

  /**
   * Schedules the next metadata load.
   */
  private synchronized void scheduleNextLoad() {
    // If another load has been scheduled, cancel it first and do not proceed if it is already
    // running (not done) and cannot be canceled. In that case, just let it finish. We want to
    // cancel the scheduled load and reschedule again at a later time since we have just refreshed.
    // And we do not want to have 2 recurring refresh cycles in parallel.
    if (refreshFrequencySeconds > 0) {
      if (loadFuture != null && !loadFuture.cancel(false) && !loadFuture.isDone())
        return;

      loadFuture = scheduler.schedule(
          new Runnable() { public void run() { loadAsync(); } },
          refreshFrequencySeconds, TimeUnit.SECONDS);
    }
  }

  /**
   * Returns the hosts for the parition key in the given table.
   *
   * @param table  the table
   * @param int    the partition key
   * @return       the hosts for the partition key, or an empty list if they cannot be determined
   *               due to cases like stale metadata cache
   */
  public List<Host> getHostsForKey(TableMetadata table, int key) {
    if (tableMap != null) {
      NavigableMap<Integer, List<Host>> partitionMap = tableMap.get(table);
      if (partitionMap != null) {
        List<Host> hosts = partitionMap.floorEntry(key).getValue();
        LOG.debug("key " + key + " -> hosts = " + Arrays.toString(hosts.toArray()));
        return hosts;
      }
    }
    return new Vector<Host>();
  }

  @Override
  public void onAdd(Host host) {
    if (allHosts.add(host)) {
      loadAsync();
    }
  }

  @Override
  public void onUp(Host host) {
    if (upHosts.add(host)) {
      loadAsync();
    }
  }

  @Override
  public void onDown(Host host) {
    if (upHosts.remove(host)) {
      loadAsync();
    }
  }

  @Override
  public void onRemove(Host host) {
    if (allHosts.remove(host)) {
      loadAsync();
    }
  }

  @Override
  public void onKeyspaceAdded(KeyspaceMetadata keyspace) {
  }

  @Override
  public void onKeyspaceRemoved(KeyspaceMetadata keyspace) {
  }

  @Override
  public void onKeyspaceChanged(KeyspaceMetadata current, KeyspaceMetadata previous) {
  }

  @Override
  public void onTableAdded(TableMetadata table) {
    try {
      if (tableMap == null || !tableMap.containsKey(table)) {
        loadAsync();
      }
    } catch (Exception e) {
      LOG.error("onTableAdded failed", e);
    }
  }

  @Override
  public void onTableRemoved(TableMetadata table) {
    try {
      if (tableMap == null || tableMap.containsKey(table)) {
        loadAsync();
      }
    } catch (Exception e) {
      LOG.error("onTableRemoved failed", e);
    }
  }

  @Override
  public void onTableChanged(TableMetadata current, TableMetadata previous) {
  }

  @Override
  public void onUserTypeAdded(UserType type) {
  }

  @Override
  public void onUserTypeRemoved(UserType type) {
  }

  @Override
  public void onUserTypeChanged(UserType current, UserType previous) {
  }

  @Override
  public void onFunctionAdded(FunctionMetadata function) {
  }

  @Override
  public void onFunctionRemoved(FunctionMetadata function) {
  }

  @Override
  public void onFunctionChanged(FunctionMetadata current, FunctionMetadata previous) {
  }

  @Override
  public void onAggregateAdded(AggregateMetadata aggregate) {
  }

  @Override
  public void onAggregateRemoved(AggregateMetadata aggregate) {
  }

  @Override
  public void onAggregateChanged(AggregateMetadata current, AggregateMetadata previous) {
  }

  @Override
  public void onMaterializedViewAdded(MaterializedViewMetadata view) {
  }

  @Override
  public void onMaterializedViewRemoved(MaterializedViewMetadata view) {
  }

  @Override
  public void onMaterializedViewChanged(MaterializedViewMetadata current,
                                        MaterializedViewMetadata previous) {
  }

  @Override
  public void onRegister(Cluster cluster) {
    session = cluster.newSession();
    clusterMetadata = cluster.getMetadata();
    scheduleNextLoad();
  }

  @Override
  public void onUnregister(Cluster cluster) {
    if (loadFuture != null) {
      loadFuture.cancel(true);
      loadFuture = null;
    }
    if (session != null) {
      session.close();
      session = null;
    }
  }
}
