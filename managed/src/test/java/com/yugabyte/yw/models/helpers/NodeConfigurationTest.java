// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.NodeConfig.Type;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class NodeConfigurationTest extends FakeDBApplication {
  private Customer customer;
  private Provider provider;

  private SettableRuntimeConfigFactory runtimeConfigFactory;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.onpremProvider(customer);
    runtimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    RuntimeConfig<Provider> providerConfig = runtimeConfigFactory.forProvider(provider);
    String nodeAgentConfig = "yb.node_agent.preflight_checks.";
    providerConfig.setValue(nodeAgentConfig + "internet_connection", "true");
    providerConfig.setValue(nodeAgentConfig + "internet_connection", "true");
    providerConfig.setValue(nodeAgentConfig + "min_ram_size_mb", "2");
    providerConfig.setValue(nodeAgentConfig + "min_python_version", "2.7");
    providerConfig.setValue(nodeAgentConfig + "prometheus_no_node_exporter", "true");
    providerConfig.setValue(nodeAgentConfig + "ports", "true");
    providerConfig.setValue(nodeAgentConfig + "pam_limits_writable", "true");
    providerConfig.setValue(nodeAgentConfig + "min_tmp_dir_space_mb", "100");
    providerConfig.setValue(nodeAgentConfig + "min_cpu_cores", "1");
    providerConfig.setValue(nodeAgentConfig + "user", "dummyUser");
  }

  @Test
  public void testNodeConfiguration() {
    // Test Internet Connection.
    NodeConfig internetConnection = new NodeConfig(Type.INTERNET_CONNECTION, "true");
    assertTrue(internetConnection.isConfigured(provider));
    internetConnection.setValue("false");
    assertFalse(internetConnection.isConfigured(provider));

    // Test Ram Size.
    NodeConfig ramSize = new NodeConfig(Type.RAM_SIZE, "6");
    assertTrue(ramSize.isConfigured(provider));
    ramSize.setValue("1");
    assertFalse(ramSize.isConfigured(provider));

    // Test Python Version.
    NodeConfig pyVersion = new NodeConfig(Type.PYTHON_VERSION, "2.9.1");
    assertTrue(pyVersion.isConfigured(provider));
    pyVersion.setValue("2.3.1");
    assertFalse(pyVersion.isConfigured(provider));

    // Test ports in JSON.
    NodeConfig ports = new NodeConfig(Type.PORTS, "{\"8080\": \"true\", \"9090\": \"true\"}");
    assertTrue(ports.isConfigured(provider));
    ports.setValue("{\"8080\": \"true\", \"9090\": \"false\"}");
    assertFalse(pyVersion.isConfigured(provider));
  }
}
