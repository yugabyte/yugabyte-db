package redis.clients.jedis;

import org.junit.Test;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;

import static org.yb.AssertionWrappers.*;

/**
 * Created by smagellan on 7/11/16.
 */
import org.junit.runner.RunWith;


import redis.clients.jedis.tests.BaseYBClassForJedis;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class HostAndPortTest extends BaseYBClassForJedis {
  @Test
  public void checkExtractParts() throws Exception {
    String host = "2a11:1b1:0:111:e111:1f11:1111:1f1e:1999";
    String port = "6379";

    assertEquals(Arrays.asList(HostAndPort.extractParts(host + ":" + port)),
            Arrays.asList(host, port));

    host = "";
    port = "";
    assertEquals(Arrays.asList(HostAndPort.extractParts(host + ":" + port)),
            Arrays.asList(host, port));

    host = "localhost";
    port = "";
    assertEquals(Arrays.asList(HostAndPort.extractParts(host + ":" + port)),
            Arrays.asList(host, port));


    host = "";
    port = "6379";
    assertEquals(Arrays.asList(HostAndPort.extractParts(host + ":" + port)),
            Arrays.asList(host, port));

    host = "11:22:33:44:55";
    port = "";
    assertEquals(Arrays.asList(HostAndPort.extractParts(host + ":" + port)),
            Arrays.asList(host, port));
  }

  @Test
  public void checkParseString() throws Exception {
    String host = "2a11:1b1:0:111:e111:1f11:1111:1f1e:1999";
    int port = 6379;
    HostAndPort hp = HostAndPort.parseString(host + ":" + Integer.toString(port));
    assertEquals(host, hp.getHost());
    assertEquals(port, hp.getPort());
  }

  @Test(expected = IllegalArgumentException.class)
  public void checkParseStringWithoutPort() throws Exception {
    String host = "localhost";
    HostAndPort.parseString(host + ":");
  }
}
