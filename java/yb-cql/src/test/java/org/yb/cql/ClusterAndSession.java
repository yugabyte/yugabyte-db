package org.yb.cql;

import com.datastax.driver.core.*;

import java.io.Closeable;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class ClusterAndSession implements Closeable, AutoCloseable {
  private static final Logger LOG = LoggerFactory.getLogger(ClusterAndSession.class);

  private Cluster cluster;
  private Session session;

  public ClusterAndSession(Cluster cluster, Session session) {
    this.cluster = cluster;
    this.session = session;
  }

  public ClusterAndSession(Cluster cluster) {
    this.cluster = cluster;
    this.session = cluster.connect();
  }

  public Cluster getCluster() {
    return cluster;
  }

  public Session getSession() {
    return session;
  }

  @Override
  public void close() {
    try {
      if (session != null) {
        session.close();
      }
    } finally {
      if (cluster != null) {
        cluster.close();
      }
    }
  }

  public ResultSet execute(String sql) {
    return session.execute(sql);
  }

  PreparedStatement prepare(String query) {
    return session.prepare(query);
  }

  ResultSet execute(Statement statement) {
    return session.execute(statement);
  }
}
