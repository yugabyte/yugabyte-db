package redis.clients.jedis.tests;

import java.util.ArrayList;
import java.util.List;

import redis.clients.jedis.HostAndPort;
import redis.clients.jedis.Protocol;

public final class HostAndPortUtil {
  public static List<HostAndPort> redisHostAndPortList = new ArrayList<HostAndPort>();
  public static List<HostAndPort> sentinelHostAndPortList = new ArrayList<HostAndPort>();
  public static List<HostAndPort> clusterHostAndPortList = new ArrayList<HostAndPort>();

  private HostAndPortUtil(){
    throw new InstantiationError( "Must not instantiate this class" );
  }

  public static List<HostAndPort> parseHosts(String envHosts,
      List<HostAndPort> existingHostsAndPorts) {

    if (null != envHosts && 0 < envHosts.length()) {

      String[] hostDefs = envHosts.split(",");

      if (null != hostDefs && 2 <= hostDefs.length) {

        List<HostAndPort> envHostsAndPorts = new ArrayList<HostAndPort>(hostDefs.length);

        for (String hostDef : hostDefs) {

          String[] hostAndPortParts = HostAndPort.extractParts(hostDef);

          if (null != hostAndPortParts && 2 == hostAndPortParts.length) {
            String host = hostAndPortParts[0];
            int port = Protocol.DEFAULT_PORT;

            try {
              port = Integer.parseInt(hostAndPortParts[1]);
            } catch (final NumberFormatException nfe) {
            }

            envHostsAndPorts.add(new HostAndPort(host, port));
          }
        }

        return envHostsAndPorts;
      }
    }

    return existingHostsAndPorts;
  }

  public static List<HostAndPort> getRedisServers() {
    return redisHostAndPortList;
  }

  public static List<HostAndPort> getSentinelServers() {
    return sentinelHostAndPortList;
  }

  public static List<HostAndPort> getClusterServers() {
    return clusterHostAndPortList;
  }
}
