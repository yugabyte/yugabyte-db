// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertSame;

import com.datastax.oss.driver.api.core.config.DriverConfigLoader;
import com.datastax.oss.driver.api.core.context.DriverContext;
import com.datastax.oss.driver.api.core.session.ProgrammaticArguments;
import java.net.InetSocketAddress;
import java.util.Map;
import org.junit.Test;

public class YcqlSessionBuilderTest {

  @Test
  public void testTranslator() {
    YcqlSessionBuilder.SecondaryToPrivateIpTranslator translator =
        new YcqlSessionBuilder.SecondaryToPrivateIpTranslator(Map.of("192.168.0.1", "10.0.0.1"));

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
