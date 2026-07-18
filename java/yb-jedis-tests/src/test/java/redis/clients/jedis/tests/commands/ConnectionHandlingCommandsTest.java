package redis.clients.jedis.tests.commands;

import static org.yb.AssertionWrappers.assertEquals;

import org.junit.Test;
import org.junit.Before;

import redis.clients.jedis.BinaryJedis;
import redis.clients.jedis.HostAndPort;
import redis.clients.jedis.tests.HostAndPortUtil;

import org.junit.runner.RunWith;


import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class ConnectionHandlingCommandsTest extends JedisCommandTestBase {
  protected HostAndPort hnp;

  @Before
  public void setUp() throws Exception {
    super.setUp();
    hnp =  new HostAndPort(
          miniCluster.getRedisContactPoints().get(0).getHostString(),
          miniCluster.getRedisContactPoints().get(0).getPort());
  }

  @Test
  public void quit() {
    assertEquals("OK", jedis.quit());
  }

  @Test
  public void binary_quit() {
    BinaryJedis bj = new BinaryJedis(hnp.getHost(), hnp.getPort());
    bj.auth("foobared");
    assertEquals("OK", bj.quit());
  }
}
