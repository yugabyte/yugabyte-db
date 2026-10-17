// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.datastax.oss.driver.api.core.CqlSessionBuilder;
import com.datastax.oss.driver.api.core.addresstranslation.AddressTranslator;
import com.datastax.oss.driver.api.core.config.DriverConfigLoader;
import com.datastax.oss.driver.api.core.context.DriverContext;
import com.datastax.oss.driver.api.core.session.ProgrammaticArguments;
import com.datastax.oss.driver.internal.core.context.DefaultDriverContext;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.net.InetSocketAddress;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Builds YCQL sessions that YBA can use with every node of a universe.
 *
 * <p>YBA connects to nodes by {@code private_ip}. Dual-NIC nodes advertise their {@code
 * secondary_private_ip} to drivers instead, so that customer clients discover the right addresses.
 * The driver replaces the contact points with the discovered addresses, which YBA usually can't
 * reach. That leaves only the control connection node usable. The address translator maps the
 * discovered addresses back to {@code private_ip}.
 */
public class YcqlSessionBuilder extends CqlSessionBuilder {

  private final Map<String, String> secondaryToPrimaryIps;

  YcqlSessionBuilder(Map<String, String> secondaryToPrimaryIps) {
    this.secondaryToPrimaryIps = ImmutableMap.copyOf(secondaryToPrimaryIps);
  }

  public static YcqlSessionBuilder forUniverse(Universe universe) {
    List<InetSocketAddress> addresses = Util.getNodesAsInet(universe.getUniverseUUID());
    if (addresses.isEmpty()) {
      throw new RuntimeException(
          "Addresses list is empty for universe " + universe.getUniverseUUID());
    }
    YcqlSessionBuilder builder = new YcqlSessionBuilder(secondaryToPrimaryIps(universe.getNodes()));
    builder.addContactPoints(addresses);
    return builder;
  }

  static Map<String, String> secondaryToPrimaryIps(Collection<NodeDetails> nodes) {
    Map<String, String> result = new HashMap<>();
    for (NodeDetails node : nodes) {
      if (node.cloudInfo == null) {
        continue;
      }
      String primaryIp = node.cloudInfo.private_ip;
      String secondaryIp = node.cloudInfo.secondary_private_ip;
      if (primaryIp != null
          && secondaryIp != null
          && !secondaryIp.equals("null")
          && !secondaryIp.equals(primaryIp)) {
        result.put(secondaryIp, primaryIp);
      }
    }
    return result;
  }

  @Override
  protected DriverContext buildContext(
      DriverConfigLoader configLoader, ProgrammaticArguments programmaticArguments) {
    // The driver only creates address translators from config by class name, so a translator
    // with per-universe state has to be provided through the context.
    return new DefaultDriverContext(configLoader, programmaticArguments) {
      @Override
      protected AddressTranslator buildAddressTranslator() {
        return new SecondaryToPrimaryIpTranslator(secondaryToPrimaryIps);
      }
    };
  }

  static class SecondaryToPrimaryIpTranslator implements AddressTranslator {
    private final Map<String, String> secondaryToPrimaryIps;

    SecondaryToPrimaryIpTranslator(Map<String, String> secondaryToPrimaryIps) {
      this.secondaryToPrimaryIps = secondaryToPrimaryIps;
    }

    @Override
    public InetSocketAddress translate(InetSocketAddress address) {
      String host =
          address.getAddress() != null
              ? address.getAddress().getHostAddress()
              : address.getHostString();
      String primaryIp = secondaryToPrimaryIps.get(host);
      return primaryIp == null ? address : new InetSocketAddress(primaryIp, address.getPort());
    }

    @Override
    public void close() {}
  }
}
