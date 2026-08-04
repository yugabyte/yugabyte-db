package redis.clients.jedis.tests;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.fail;

import org.junit.After;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import redis.clients.jedis.Connection;
import redis.clients.jedis.HostAndPort;
import redis.clients.jedis.Protocol.Command;
import redis.clients.jedis.exceptions.JedisConnectionException;

import org.junit.runner.RunWith;

import redis.clients.jedis.tests.BaseYBClassForJedis;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class ConnectionTest extends BaseYBClassForJedis {
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
    client.disconnect();
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

  @Test
  public void checkCloseable() {
    client.setHost(hnp.getHost());
    client.setPort(hnp.getPort());
    client.connect();
    client.close();
  }

  @Test
  @Ignore public void getErrorAfterConnectionReset() throws Exception {
    class TestConnection extends Connection {
      public TestConnection() { super(hnp.getHost(), hnp.getPort()); }

      @Override
      protected Connection sendCommand(Command cmd, byte[]... args) {
        return super.sendCommand(cmd, args);
      }
    }

    TestConnection conn = new TestConnection();

    try {
      conn.sendCommand(Command.HMSET, new byte[1024 * 1024 + 1][0]);
      fail("Should throw exception");
    } catch (JedisConnectionException jce) {
      assertEquals("ERR Protocol error: invalid multibulk length", jce.getMessage());
    }
  }
}
