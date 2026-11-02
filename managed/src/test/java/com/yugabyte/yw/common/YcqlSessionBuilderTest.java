// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertSame;

import com.datastax.oss.driver.api.core.config.DriverConfigLoader;
import com.datastax.oss.driver.api.core.context.DriverContext;
import com.datastax.oss.driver.api.core.session.ProgrammaticArguments;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.net.InetSocketAddress;
import java.util.List;
import java.util.Map;
import org.junit.Test;

public class YcqlSessionBuilderTest {

  private static NodeDetails node(String privateIp, String secondaryPrivateIp) {
    NodeDetails node = new NodeDetails();
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = privateIp;
    node.cloudInfo.secondary_private_ip = secondaryPrivateIp;
    return node;
  }

  @Test
  public void testSecondaryToPrimaryIps() {
    NodeDetails noCloudInfo = new NodeDetails();
    Map<String, String> ips =
        YcqlSessionBuilder.secondaryToPrimaryIps(
            List.of(
                node("10.0.0.1", "192.168.0.1"),
                node("10.0.0.2", "192.168.0.2"),
                node("10.0.0.3", null),
                node("10.0.0.4", "null"),
                node("10.0.0.5", "10.0.0.5"),
                node(null, "192.168.0.6"),
                noCloudInfo));
    assertEquals(Map.of("192.168.0.1", "10.0.0.1", "192.168.0.2", "10.0.0.2"), ips);
  }

  @Test
  public void testTranslator() {
    YcqlSessionBuilder.SecondaryToPrimaryIpTranslator translator =
        new YcqlSessionBuilder.SecondaryToPrimaryIpTranslator(Map.of("192.168.0.1", "10.0.0.1"));

    assertEquals(
        new InetSocketAddress("10.0.0.1", 9042),
        translator.translate(new InetSocketAddress("192.168.0.1", 9042)));
    // Keeps the port reported by the node.
    assertEquals(
        new InetSocketAddress("10.0.0.1", 9142),
        translator.translate(new InetSocketAddress("192.168.0.1", 9142)));
    // Unresolved addresses are matched by host string.
    assertEquals(
        new InetSocketAddress("10.0.0.1", 9042),
        translator.translate(InetSocketAddress.createUnresolved("192.168.0.1", 9042)));

    InetSocketAddress unknown = new InetSocketAddress("192.168.0.9", 9042);
    assertSame(unknown, translator.translate(unknown));
  }

  @Test
  public void testContextUsesTranslator() {
    YcqlSessionBuilder builder = new YcqlSessionBuilder(Map.of("192.168.0.1", "10.0.0.1"));
    DriverContext context =
        builder.buildContext(
            DriverConfigLoader.fromDefaults(getClass().getClassLoader()),
            ProgrammaticArguments.builder().build());
    assertEquals(
        new InetSocketAddress("10.0.0.1", 9042),
        context.getAddressTranslator().translate(new InetSocketAddress("192.168.0.1", 9042)));
  }
}
