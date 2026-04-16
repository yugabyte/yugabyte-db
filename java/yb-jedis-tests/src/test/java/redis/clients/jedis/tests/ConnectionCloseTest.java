package redis.clients.jedis.tests;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import redis.clients.jedis.Connection;
import redis.clients.jedis.HostAndPort;
import redis.clients.jedis.exceptions.JedisConnectionException;

import org.junit.runner.RunWith;


import redis.clients.jedis.tests.BaseYBClassForJedis;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class ConnectionCloseTest extends BaseYBClassForJedis {
  private static HostAndPort hnp;
  private Connection client;

  @Before
  public void setUp() throws Exception {
    hnp =  new HostAndPort(
          miniCluster.getRedisContactPoints().get(0).getHostString(),
          miniCluster.getRedisContactPoints().get(0).getPort());
    client = new Connection();
  }

  @After
  public void tearDown() throws Exception {
    client.close();
  }

  @Test(expected = JedisConnectionException.class)
  public void checkUnkownHost() {
    client.setHost("someunknownhost");
    client.connect();
  }

  @Test(expected = JedisConnectionException.class)
  public void checkWrongPort() {
    client.setHost(hnp.getHost());
    client.setPort(hnp.getPort() + 100);
    client.connect();
  }

  @Test
  public void connectIfNotConnectedWhenSettingTimeoutInfinite() {
    client.setHost(hnp.getHost());
    client.setPort(hnp.getPort());
    client.setTimeoutInfinite();
  }
}
