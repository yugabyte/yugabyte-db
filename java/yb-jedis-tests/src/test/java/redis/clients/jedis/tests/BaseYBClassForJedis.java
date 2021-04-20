package redis.clients.jedis.tests;

import java.net.InetSocketAddress;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import org.junit.Before;

import com.yugabyte.jedis.BaseJedisTest;

import redis.clients.jedis.HostAndPort;
import redis.clients.jedis.Jedis;

public abstract class BaseYBClassForJedis extends BaseJedisTest {

  public BaseYBClassForJedis() {
    super(JedisClientType.JEDIS);
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("redis_password_caching_duration_ms", "0");
    return flagMap;
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
