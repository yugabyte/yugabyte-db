package redis.clients.jedis.tests.commands;

import static org.yb.AssertionWrappers.assertArrayEquals;
import static org.yb.AssertionWrappers.assertEquals;

import java.net.InetSocketAddress;
import java.util.List;
import java.util.Set;

import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.client.YBClient;
import redis.clients.jedis.tests.BaseYBClassForJedis;

import redis.clients.jedis.HostAndPort;
import redis.clients.jedis.Jedis;
import redis.clients.jedis.tests.HostAndPortUtil;

import static junit.framework.TestCase.assertTrue;

public abstract class JedisCommandTestBase extends BaseYBClassForJedis {
  private static final Logger LOG = LoggerFactory.getLogger(JedisCommandTestBase.class);
  protected static final String DEFAULT_DB_NAME = "0";
  protected static HostAndPort hnp;

  protected Jedis jedis;

  @Before
  public void setUp() throws Exception {
    List<InetSocketAddress> redisContactPoints = miniCluster.getRedisContactPoints();
    assertTrue(redisContactPoints.size() > 0);
    hnp = new HostAndPort(redisContactPoints.get(0).getHostString(),
                          redisContactPoints.get(0).getPort());

    jedis = new Jedis(hnp.getHost(), hnp.getPort(), 60000);
    jedis.connect();
    jedis.auth("foobared");
    jedis.configSet("timeout", "300");
    jedis.flushAll();
  }

  @After
  public void tearDown() {
    jedis.disconnect();
  }

  protected Jedis createJedis() {
    Jedis j = new Jedis(hnp.getHost(), hnp.getPort());
    j.connect();
    j.auth("foobared");
    j.flushAll();
    return j;
  }

  protected boolean arrayContains(List<byte[]> array, byte[] expected) {
    for (byte[] a : array) {
      try {
        assertArrayEquals(a, expected);
        return true;
      } catch (AssertionError e) {

      }
    }
    return false;
  }

  protected boolean setContains(Set<byte[]> set, byte[] expected) {
    for (byte[] a : set) {
      try {
        assertArrayEquals(a, expected);
        return true;
      } catch (AssertionError e) {

      }
    }
    return false;
  }
}
