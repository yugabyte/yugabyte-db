// Copyright (c) YugaByte, Inc.
package com.yugabyte.cql;

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.ColumnDefinitions;
import com.datastax.driver.core.DataType;
import com.datastax.driver.core.Host;
import com.datastax.driver.core.HostDistance;
import com.datastax.driver.core.KeyspaceMetadata;
import com.datastax.driver.core.Metadata;
import com.datastax.driver.core.PreparedId;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.Statement;
import com.datastax.driver.core.TableMetadata;
import com.datastax.driver.core.policies.ChainableLoadBalancingPolicy;
import com.datastax.driver.core.policies.LoadBalancingPolicy;
import com.datastax.driver.core.policies.DCAwareRoundRobinPolicy;

import org.apache.log4j.Logger;

import java.lang.reflect.Field;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.Channels;
import java.nio.channels.WritableByteChannel;
import java.util.Collection;
import java.util.Iterator;

/**
 * The load-balancing policy to direct a statement to the hosts where the tablet for the partition
 * key resides, with the tablet leader host at the beginning of the host list as the preferred
 * host. The hosts are found by computing the hash key and looking them up in the partition metadata
 * from the system.partitions table.
 *
 * @see PartitionMetadata
 */
public class PartitionAwarePolicy implements ChainableLoadBalancingPolicy {

  private final LoadBalancingPolicy childPolicy;
  private volatile Metadata clusterMetadata;
  private volatile PartitionMetadata partitionMetadata;

  private static final Logger LOG = Logger.getLogger(PartitionAwarePolicy.class);

  // The PreparedId.routingKeyIndexes field to retrieve the bind indexes of the partition columns
  // in a statement. Access it via reflection since the field is not publicly accessible currently.
  // TODO: add a public API in Cassandra driver to access the bind indexes.
  private static Field routingKeyIndexesField;

  static {
    try {
      routingKeyIndexesField = PreparedId.class.getDeclaredField("routingKeyIndexes");
      routingKeyIndexesField.setAccessible(true);
    } catch (ReflectiveOperationException e) {
      throw new InternalError("Set PreparedId.routingKeyIndexes accessible failed: " +
                              e.getMessage());
    }
  }

  /**
   * Creates a new {@code PartitionAware} policy.
   *
   * @param childPolicy  the load balancing policy to wrap with partition awareness
   */
  public PartitionAwarePolicy(LoadBalancingPolicy childPolicy) {
    this.childPolicy = childPolicy;
  }

  /**
   * Creates a new {@code PartitionAware} policy with additional default data-center awareness.
   */
  public PartitionAwarePolicy() {
    this(new DCAwareRoundRobinPolicy.Builder().build());
  }

  @Override
  public void init(Cluster cluster, Collection<Host> hosts) {
    clusterMetadata = cluster.getMetadata();
    partitionMetadata = new PartitionMetadata(cluster);
    childPolicy.init(cluster, hosts);
  }

  @Override
  public LoadBalancingPolicy getChildPolicy() {
    return childPolicy;
  }

  /**
   * Returns the hash key for the given bytes. The hash key is an unsigned 16-bit number.
   *
   * @param bytes  the bytes to calculate the hash key
   * @return       the hash key
   */
  private static int getKey(byte bytes[]) {
    final long SEED = 97;
    long h = Jenkins.hash64(bytes, SEED);
    long h1 = h >>> 48;
    long h2 = 3 * (h >>> 32);
    long h3 = 5 * (h >>> 16);
    long h4 = 7 * (h & 0xffff);
    return (int)((h1 ^ h2 ^ h3 ^ h4) & 0xffff);
  }

  /**
   * Returns the hash key for the given bound statement. The hash key can be determined only for
   * DMLs and when the partition key is specifed in the bind variables.
   *
   * @param bytes  the bytes to calculate the hash key
   * @return       the hash key for the statement, or -1 when hash key cannot be determined
   */
  static int getKey(BoundStatement stmt) {
    PreparedStatement pstmt = stmt.preparedStatement();
    PreparedId id = pstmt.getPreparedId();
    int hashIndexes[];

    // Retrieve the bind indexes of the hash columns in the bound statement.
    try {
      hashIndexes = (int[])routingKeyIndexesField.get(id);
    } catch (IllegalAccessException e) {
      throw new InternalError("Get PreparedId.routingKeyIndexes failed: " + e.getMessage());
    }

    // Return if no hash key indexes are found, such as when the hash column values are literal
    // constants.
    if (hashIndexes == null || hashIndexes.length == 0) {
      return -1;
    }

    // Compute the hash key bytes, i.e. <h1><h2>...<h...>.
    try {
      ByteArrayOutputStream bs = new ByteArrayOutputStream();
      WritableByteChannel channel = Channels.newChannel(bs);
      ColumnDefinitions variables = pstmt.getVariables();
      for (int i = 0; i < hashIndexes.length; i++) {
        DataType.Name typeName = variables.getType(hashIndexes[i]).getName();
        ByteBuffer value = stmt.getBytesUnsafe(hashIndexes[i]);
        switch (typeName) {
          case TINYINT:
          case SMALLINT:
          case INT:
          case BIGINT: {
            // Flip the MSB for integer columns.
            ByteBuffer bb = ByteBuffer.allocate(value.remaining());
            bb.put((byte)(value.get() ^ 0x80));
            while (value.remaining() > 0) {
              bb.put(value.get());
            }
            bb.flip();
            value = bb;
            break;
          }
          case TIMESTAMP: {
            // Multiply the timestamp's int64 value by 1000 to adjust the precision in conjunction
            // to flipping the MSB.
            ByteBuffer bb = ByteBuffer.allocate(8);
            bb.putLong((value.getLong() * 1000) ^ 0x8000000000000000L);
            bb.flip();
            value = bb;
            break;
          }
          case ASCII:
          case TEXT:
          case VARCHAR:
          case BLOB:
          case INET:
          case UUID:
          case TIMEUUID: {
            // For BINARY-based columns, escape "\0x00" with "\0x00\0x01" and append "\0x00\0x00"
            // delimiter unless it is the last column.
            if (i != hashIndexes.length - 1) {
              int zeroCount = 0;
              for (int j = 0; j < value.remaining(); j++) {
                if (value.get(j) == (byte)0x00) {
                  zeroCount++;
                }
              }
              ByteBuffer bb = ByteBuffer.allocate(value.remaining() + zeroCount + 2);
              while (value.remaining() > 0) {
                byte b = value.get();
                bb.put(b);
                if (b == (byte)0x00) {
                  bb.put((byte)0x01);
                }
              }
              bb.putShort((short)0x0000);
              bb.flip();
              value = bb;
            }
            break;
          }
          case BOOLEAN:
          case COUNTER:
          case CUSTOM:
          case DATE:
          case DECIMAL:
          case DOUBLE:
          case FLOAT:
          case LIST:
          case MAP:
          case SET:
          case TIME:
          case TUPLE:
          case UDT:
          case VARINT:
            throw new UnsupportedOperationException("Datatype " + typeName.toString() +
                                                    " not supported in a primary key column");
        }
        channel.write(value);
      }
      channel.close();
      return getKey(bs.toByteArray());
    } catch (IOException e) {
      // IOException should not happen at all given we are writing to the in-memory buffer only. So
      // if it does happen, we just want to log the error but fallback to the default set of hosts.
      LOG.error("hash key encoding failed", e);
      return -1;
    }
  }

  /**
   * An iterator that returns hosts to executing a given statement, selecting only the hosts that
   * are up and local from the given hosts that host the statement's partition key, and then the
   * ones from the child policy.
   */
  private class UpHostIterator implements Iterator<Host> {

    private final String loggedKeyspace;
    private final Statement statement;
    private final Collection<Host> hosts;
    private final Iterator<Host> iterator;
    private Iterator<Host> childIterator;
    private Host nextHost;

    /**
     * Creates a new {@code UpHostIterator}.
     *
     * @param loggedKeyspace  the logged keyspace of the statement
     * @param statement       the statement
     * @param hosts           the hosts that host the statement's partition key
     */
    public UpHostIterator(String loggedKeyspace, Statement statement, Collection<Host> hosts) {
      this.loggedKeyspace = loggedKeyspace;
      this.statement = statement;
      this.hosts = hosts;
      this.iterator = hosts.iterator();
    }

    @Override
    public boolean hasNext() {

      while (iterator.hasNext()) {
        nextHost = iterator.next();
        if (nextHost.isUp() && childPolicy.distance(nextHost) == HostDistance.LOCAL)
          return true;
      }

      if (childIterator == null)
        childIterator = childPolicy.newQueryPlan(loggedKeyspace, statement);

      while (childIterator.hasNext()) {
        nextHost = childIterator.next();
        // Skip host if it is a local host that we have already returned earlier.
        if (!hosts.contains(nextHost) || childPolicy.distance(nextHost) != HostDistance.LOCAL)
          return true;
      }

      return false;
    }

    @Override
    public Host next() {
      return nextHost;
    }
  }

  /**
   * Gets the query plan for a {@code BoundStatement}.
   *
   * @param loggedKeyspace  the logged keyspace of the statement
   * @param statement       the statement
   * @return                the query plan, or null when no plan can be determined
   */
  private Iterator<Host> getQueryPlan(String loggedKeyspace, BoundStatement statement) {
    PreparedStatement pstmt = statement.preparedStatement();
    String query = pstmt.getQueryString();
    ColumnDefinitions variables = pstmt.getVariables();

    // Look up the hosts for the partition key. Skip statements that do not have bind variables.
    // Skip also PartitionMetadata's own query to avoid infinite loop.
    if (variables.size() == 0 || query.equals(PartitionMetadata.PARTITIONS_QUERY))
      return null;
    LOG.debug("getQueryPlan: keyspace = " + loggedKeyspace + ", " + "query = " + query);
    KeyspaceMetadata keyspace = clusterMetadata.getKeyspace(variables.getKeyspace(0));
    if (keyspace == null)
      return null;
    TableMetadata table = keyspace.getTable(variables.getTable(0));
    if (table == null)
      return null;
    int key = getKey(statement);
    if (key < 0)
      return null;

    Collection<Host> hosts = partitionMetadata.getHostsForKey(table, key);
    return new UpHostIterator(loggedKeyspace, statement, hosts);
  }

  /**
   * Gets the query plan for a {@code BatchStatement}.
   *
   * @param loggedKeyspace  the logged keyspace of the statement
   * @param statement       the statement
   * @return                the query plan, or null when no plan can be determined
   */
  private Iterator<Host> getQueryPlan(String loggedKeyspace, BatchStatement batch) {
    for (Statement statement : batch.getStatements()) {
      if (statement instanceof BoundStatement) {
        Iterator<Host> plan = getQueryPlan(loggedKeyspace, (BoundStatement)statement);
        if (plan != null)
          return plan;
      }
    }
    return null;
  }

  @Override
  public Iterator<Host> newQueryPlan(String loggedKeyspace, Statement statement) {
    Iterator<Host> plan = null;
    if (statement instanceof BoundStatement) {
      plan = getQueryPlan(loggedKeyspace, (BoundStatement)statement);
    } else if (statement instanceof BatchStatement) {
      plan = getQueryPlan(loggedKeyspace, (BatchStatement)statement);
    }
    return (plan != null) ? plan : childPolicy.newQueryPlan(loggedKeyspace, statement);
  }

  @Override
  public HostDistance distance(Host host) {
    return childPolicy.distance(host);
  }

  @Override
  public void onAdd(Host host) {
    childPolicy.onAdd(host);
  }

  @Override
  public void onUp(Host host) {
    childPolicy.onUp(host);
  }

  @Override
  public void onDown(Host host) {
    childPolicy.onDown(host);
  }

  @Override
  public void onRemove(Host host) {
    childPolicy.onRemove(host);
  }

  @Override
  public void close() {
    childPolicy.close();
  }
}
