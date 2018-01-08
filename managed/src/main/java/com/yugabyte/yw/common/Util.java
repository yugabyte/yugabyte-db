// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import com.google.common.base.Functions;
import com.google.common.base.Joiner;
import com.google.common.collect.Lists;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import java.net.InetSocketAddress;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Random;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class Util {
  public static final Logger LOG = LoggerFactory.getLogger(Util.class);

  /**
   * Convert a list of {@link HostAndPort} objects to a comma separate string.
   *
   * @param hostsAndPorts A list of {@link HostAndPort} objects.
   * @return Comma separate list of "host:port" pairs.
   */
  public static String hostsAndPortsToString(List<HostAndPort> hostsAndPorts) {
    return Joiner.on(",").join(Lists.transform(hostsAndPorts, Functions.toStringFunction()));
  }

  // Create the list which contains the outcome of 'a - b', i.e., elements in a but not in b.
  public static <T> List<T> ListDiff(List<T> a, List<T> b) {
    List<T> diff = new ArrayList<T> (a.size());
    diff.addAll(a);
    diff.removeAll(b);

    return diff;
  }

  public static String CHARACTERS="ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  // Helper to create a random string of length numChars from characters in CHARACTERS.
  public static String randomString(Random rng, int numChars) {
    if (numChars < 1) {
      return "";
    }

    StringBuilder sb = new StringBuilder();
    for (int i = 0; i < numChars; i++) {
      sb.append(CHARACTERS.charAt(rng.nextInt(CHARACTERS.length())));
    }
    return sb.toString();
  }

  // In place of apache StringUtils.indexInArray(), check if needle is in haystack.
  public static boolean existsInList(String needle, List<String> haystack) {
    for (String something : haystack) {
      if (something.equals(needle))
        return true;
    }

    return false;
  }

  // Convert node details to list of host/ports.
  public static List<HostAndPort> getHostPortList(Collection<NodeDetails> nodes) {
    List<HostAndPort> curServers = new ArrayList<HostAndPort>();
    for (NodeDetails node : nodes) {
      curServers.add(HostAndPort.fromParts(node.cloudInfo.private_ip, node.masterRpcPort));
    }
    return curServers;
  }

  /**
   * Returns a list of Inet address objects in the proxy tier. This is needed by Cassandra clients.
   */
  public static List<InetSocketAddress> getNodesAsInet(UUID universeUUID) {
    Universe universe = Universe.get(universeUUID);
    List<InetSocketAddress> inetAddrs = new ArrayList<InetSocketAddress>();
    for (String address : universe.getYQLServerAddresses().split(",")) {
      String[] splitAddress = address.split(":");
      String privateIp = splitAddress[0];
      int yqlRPCPort = Integer.parseInt(splitAddress[1]);
      inetAddrs.add(new InetSocketAddress(privateIp, yqlRPCPort));
    }
    return inetAddrs;
  }

  /**
   * Returns UUID representation of ID string without dashes
   * For eg. 87d2d6473b3645f7ba56d9e3f7dae239 becomes 87d2d647-3b36-45f7-ba56-d9e3f7dae239
   */
  public static UUID getUUIDRepresentation(String id) {
    if (id.length() != 32 || id.contains("-")) {
      return null;
    } else {
      String uuidWithHyphens = id.replaceAll(
        "(\\w{8})(\\w{4})(\\w{4})(\\w{4})(\\w{12})",
        "$1-$2-$3-$4-$5");
      return UUID.fromString(uuidWithHyphens);
    }
  }
  
  /**
   * Returns a map of nodes in the ToBeAdded state in the given nodeDetailsSet
   * @param taskParams taskParams for the Create/Edit operation
   * @return Map of AZUUID to number of desired nodes in the AZ
   */
  public static HashMap<UUID, Integer> toBeAddedAzUuidToNumNodes(UniverseDefinitionTaskParams taskParams) {
    HashMap<UUID, Integer> toBeAddedazUUIDToNumNodes = new HashMap<>();
    if (taskParams == null || taskParams.nodeDetailsSet.isEmpty()) {
      return toBeAddedazUUIDToNumNodes;
    }
    for (NodeDetails currentNode: taskParams.nodeDetailsSet) {
      if (currentNode.state == NodeDetails.NodeState.ToBeAdded) {
        UUID currentAZUUID = currentNode.azUuid;
        toBeAddedazUUIDToNumNodes.put(currentAZUUID, toBeAddedazUUIDToNumNodes.getOrDefault(currentAZUUID, 0) + 1);
      }
    }
    return toBeAddedazUUIDToNumNodes;
  }
}
