package redis.clients.jedis.tests;

import com.yugabyte.jedis.BaseJedisTest;

import org.junit.Before;
import org.junit.BeforeClass;
import redis.clients.jedis.HostAndPort;
import redis.clients.jedis.Jedis;

import java.net.InetSocketAddress;
import java.util.ArrayList;
import java.util.List;

public abstract class BaseYBClassForJedis extends BaseJedisTest {

  public BaseYBClassForJedis() {
    super(JedisClientType.JEDIS);
  }

  @BeforeClass
  public static void disablePasswordCachingOnTServers() {
    tserverArgs.add("--redis_password_caching_duration_ms=0");
  }

  @Before
  public void setJedisPassword() throws Exception {
    List<HostAndPort> hostAndPortList = new ArrayList<HostAndPort>();
    for (InetSocketAddress pts : miniCluster.getRedisContactPoints()) {
      hostAndPortList.add(new HostAndPort(pts.getHostString(), pts.getPort()));
    }
    HostAndPortUtil.redisHostAndPortList = hostAndPortList;
    HostAndPortUtil.clusterHostAndPortList = hostAndPortList;
    HostAndPortUtil.sentinelHostAndPortList = hostAndPortList;
    ((Jedis)jedis_client).configSet("requirepass", "foobared");
  }

}
