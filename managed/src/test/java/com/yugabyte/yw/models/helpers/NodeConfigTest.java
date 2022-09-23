// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.NodeConfig.Type;
import com.yugabyte.yw.models.helpers.NodeConfig.ValidationResult;
import java.util.EnumSet;
import java.util.HashSet;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class NodeConfigTest extends FakeDBApplication {
  private Customer customer;
  private Provider provider;

  private SettableRuntimeConfigFactory runtimeConfigFactory;
  private NodeConfigValidator nodeConfigValidator;
  private AccessKey accessKey;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.onpremProvider(customer);
    runtimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    nodeConfigValidator = new NodeConfigValidator(runtimeConfigFactory);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.publicKey = "/path/to/public.key";
    keyInfo.privateKey = "/path/to/private.key";
    keyInfo.vaultFile = "/path/to/vault_file";
    keyInfo.vaultPasswordFile = "/path/to/vault_password";
    keyInfo.installNodeExporter = false;
    accessKey = AccessKey.create(provider.uuid, "access-code1", keyInfo);
    InstanceType.upsert(
        provider.uuid, "c5.xlarge", 4 /* cores */, 10.0 /* mem in GB */, new InstanceTypeDetails());
    setProviderRuntimeConfig("internet_connection", "true");
    setProviderRuntimeConfig("min_python_version", "2.7");
    setProviderRuntimeConfig("prometheus_no_node_exporter", "true");
    setProviderRuntimeConfig("ports", "true");
    setProviderRuntimeConfig("pam_limits_writable", "true");
    setProviderRuntimeConfig("min_tmp_dir_space_mb", "100");
    setProviderRuntimeConfig("user", "dummyUser");
  }

  private void setProviderRuntimeConfig(String pathSuffix, String value) {
    RuntimeConfig<Provider> providerConfig = runtimeConfigFactory.forProvider(provider);
    String nodeAgentConfig = "yb.node_agent.preflight_checks.";
    providerConfig.setValue(nodeAgentConfig + pathSuffix.toLowerCase(), value);
  }

  @Test
  public void testNodeConfigs() {
    NodeInstanceData instanceData = new NodeInstanceData();
    instanceData.instanceType = "c5.xlarge";
    instanceData.nodeConfigs = new HashSet<>();

    NodeConfig internetConnection = new NodeConfig(Type.INTERNET_CONNECTION, "true");
    NodeConfig ramSize = new NodeConfig(Type.RAM_SIZE, "10240" /* MB */);
    NodeConfig cpuCores = new NodeConfig(Type.CPU_CORES, "6");
    NodeConfig pyVersion = new NodeConfig(Type.PYTHON_VERSION, "2.9.1");
    NodeConfig ports = new NodeConfig(Type.PORTS, "{\"8080\": \"true\", \"9090\": \"true\"}");
    NodeConfig noNodeExporter = new NodeConfig(Type.PROMETHEUS_NO_NODE_EXPORTER, "true");

    instanceData.nodeConfigs.add(internetConnection);
    instanceData.nodeConfigs.add(ramSize);
    instanceData.nodeConfigs.add(cpuCores);
    instanceData.nodeConfigs.add(pyVersion);
    instanceData.nodeConfigs.add(ports);
    instanceData.nodeConfigs.add(noNodeExporter);
    Map<Type, ValidationResult> results =
        nodeConfigValidator.validateNodeConfigs(provider, instanceData);
    // All the defined types must be present.
    assertEquals(EnumSet.allOf(Type.class), results.keySet());
    // Get the count of valid configs.
    long validCount = results.values().stream().filter(r -> r.isValid()).count();
    assertEquals(6, validCount);
    for (NodeConfig nodeConfig : instanceData.nodeConfigs) {
      ValidationResult result = results.get(nodeConfig.getType());
      assertTrue(result.isValid());
      assertEquals(nodeConfig.getType().getDescription(), result.getDescription());
      if (nodeConfig.getType() == Type.PROMETHEUS_NO_NODE_EXPORTER
          || nodeConfig.getType() == Type.NTP_SERVICE_STATUS) {
        assertFalse(result.isRequired());
      } else {
        assertTrue(result.isRequired());
      }
    }

    // Min RAM size in InstanceType is 10GB.
    // This must fail.
    ramSize.setValue("9000");
    accessKey.getKeyInfo().installNodeExporter = true;
    accessKey.save();
    results = nodeConfigValidator.validateNodeConfigs(provider, instanceData);
    assertEquals(EnumSet.allOf(Type.class), results.keySet());
    validCount = results.values().stream().filter(r -> r.isValid()).count();
    assertEquals(5, validCount);
    ValidationResult result = results.get(Type.RAM_SIZE);
    assertFalse(result.isValid());
    assertTrue(result.isRequired());
    result = results.get(Type.PROMETHEUS_NO_NODE_EXPORTER);
    assertTrue(result.isValid());
    assertTrue(result.isRequired());

    ramSize.setValue("10240");
    // One port is made false.
    // This must fail.
    ports.setValue("{\"8080\": \"true\", \"9090\": \"false\"}");
    results = nodeConfigValidator.validateNodeConfigs(provider, instanceData);
    assertEquals(EnumSet.allOf(Type.class), results.keySet());
    validCount = results.values().stream().filter(r -> r.isValid()).count();
    assertEquals(5, validCount);
    result = results.get(Type.PORTS);
    assertFalse(result.isValid());
    assertTrue(result.isRequired());
  }
}
