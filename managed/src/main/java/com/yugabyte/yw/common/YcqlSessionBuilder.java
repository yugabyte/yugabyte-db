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
import java.net.InetSocketAddress;
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

  private final Map<String, String> secondaryToPrivateIps;

  YcqlSessionBuilder(Map<String, String> secondaryToPrivateIps) {
    this.secondaryToPrivateIps = ImmutableMap.copyOf(secondaryToPrivateIps);
  }

  public static YcqlSessionBuilder forUniverse(Universe universe) {
    List<InetSocketAddress> addresses = Util.getNodesAsInet(universe);
    if (addresses.isEmpty()) {
      throw new RuntimeException(
          "Addresses list is empty for universe " + universe.getUniverseUUID());
    }
    YcqlSessionBuilder builder =
        new YcqlSessionBuilder(Util.getSecondaryToPrivateIps(universe.getNodes()));
    builder.addContactPoints(addresses);
    return builder;
  }

  @Override
  protected DriverContext buildContext(
      DriverConfigLoader configLoader, ProgrammaticArguments programmaticArguments) {
    // The driver only creates address translators from config by class name, so a translator
    // with per-universe state has to be provided through the context.
    return new DefaultDriverContext(configLoader, programmaticArguments) {
      @Override
      protected AddressTranslator buildAddressTranslator() {
        return new SecondaryToPrivateIpTranslator(secondaryToPrivateIps);
      }
    };
  }

  static class SecondaryToPrivateIpTranslator implements AddressTranslator {
    private final Map<String, String> secondaryToPrivateIps;

    SecondaryToPrivateIpTranslator(Map<String, String> secondaryToPrivateIps) {
      this.secondaryToPrivateIps = secondaryToPrivateIps;
    }

    @Override
    public InetSocketAddress translate(InetSocketAddress address) {
      String host =
          address.getAddress() != null
              ? address.getAddress().getHostAddress()
              : address.getHostString();
      String privateIp = secondaryToPrivateIps.get(host);
      return privateIp == null ? address : new InetSocketAddress(privateIp, address.getPort());
    }

    @Override
    public void close() {}
  }
}
