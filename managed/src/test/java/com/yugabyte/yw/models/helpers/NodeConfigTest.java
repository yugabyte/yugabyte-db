// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeAgentClient;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
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
import java.util.HashSet;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class NodeConfigTest extends FakeDBApplication {
  private Customer customer;
  private Provider provider;

  private SettableRuntimeConfigFactory runtimeConfigFactory;
  private NodeConfigValidator nodeConfigValidator;
  private NodeAgentClient nodeAgentClient;
  private AccessKey accessKey;

  @Mock ShellProcessHandler shellProcessHandler;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.onpremProvider(customer);
    runtimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    nodeAgentClient = app.injector().instanceOf(NodeAgentClient.class);
    nodeConfigValidator =
        new NodeConfigValidator(runtimeConfigFactory, shellProcessHandler, nodeAgentClient);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.publicKey = "/path/to/public.key";
    keyInfo.privateKey = "/path/to/private.key";
    keyInfo.vaultFile = "/path/to/vault_file";
    keyInfo.vaultPasswordFile = "/path/to/vault_password";
    keyInfo.installNodeExporter = false;
    keyInfo.skipProvisioning = false;
    keyInfo.sshPort = 22;
    accessKey = AccessKey.create(provider.getUuid(), "access-code1", keyInfo);
    InstanceType.upsert(
        provider.getUuid(),
        "c5.xlarge",
        4 /* cores */,
        10.0 /* mem in GB */,
        new InstanceTypeDetails());
    setProviderRuntimeConfig("min_python_version", "2.7");
    setProviderRuntimeConfig("min_tmp_dir_space_mb", "100");
    setProviderRuntimeConfig("user", "dummyUser");

    ShellResponse response = ShellResponse.create(0, "{}");
    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class))).thenReturn(response);
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
    NodeConfig sshPort = new NodeConfig(Type.SSH_PORT, "{\"22\": \"true\"}");
    NodeConfig noNodeExporter = new NodeConfig(Type.PROMETHEUS_NO_NODE_EXPORTER, "true");
    NodeConfig mountPointsWritable =
        new NodeConfig(Type.MOUNT_POINTS_WRITABLE, "{\"/mnt\": \"true\"}");

    instanceData.nodeConfigs.add(internetConnection);
    instanceData.nodeConfigs.add(ramSize);
    instanceData.nodeConfigs.add(cpuCores);
    instanceData.nodeConfigs.add(pyVersion);
    instanceData.nodeConfigs.add(sshPort);
    instanceData.nodeConfigs.add(mountPointsWritable);
    Map<Type, ValidationResult> results =
        nodeConfigValidator.validateNodeConfigs(provider, instanceData, true);
    // All the defined types must be present.
    assertEquals(instanceData.nodeConfigs.size() + 1, results.keySet().size());
    // Get the count of valid configs.
    long validCount = results.values().stream().filter(r -> r.isValid()).count();
    assertEquals(7, validCount);
    for (NodeConfig nodeConfig : instanceData.nodeConfigs) {
      ValidationResult result = results.get(nodeConfig.getType());
      assertTrue(result.isValid());
      assertEquals(nodeConfig.getType().getDescription(), result.getDescription());
      if (nodeConfig.getType() == Type.NTP_SERVICE_STATUS) {
        assertFalse(result.isRequired());
      } else {
        assertTrue(result.isRequired());
      }
    }

    // Min RAM size in InstanceType is 10GB.
    // This must fail.
    ramSize.setValue("9000");
    results = nodeConfigValidator.validateNodeConfigs(provider, instanceData, true);
    assertEquals(instanceData.nodeConfigs.size() + 1, results.keySet().size());
    validCount = results.values().stream().filter(r -> r.isValid()).count();
    assertEquals(6, validCount);
    ValidationResult result = results.get(Type.RAM_SIZE);
    assertFalse(result.isValid());
    assertTrue(result.isRequired());

    ramSize.setValue("10240");
    // One mount point is not writable
    // This must fail.
    mountPointsWritable.setValue("{\"/mnt\": \"true\", \"/mnt1\": \"false\"}");
    results = nodeConfigValidator.validateNodeConfigs(provider, instanceData, true);
    assertEquals(instanceData.nodeConfigs.size() + 1, results.keySet().size());
    validCount = results.values().stream().filter(r -> r.isValid()).count();
    assertEquals(6, validCount);
    result = results.get(Type.MOUNT_POINTS_WRITABLE);
    assertFalse(result.isValid());
    assertTrue(result.isRequired());

    mountPointsWritable.setValue("{\"/mnt\": \"true\"}");
    // Check that there is no node exporter running when we are going to install node exporter.
    accessKey.getKeyInfo().installNodeExporter = true;
    accessKey.save();
    instanceData.nodeConfigs.add(noNodeExporter);
    results = nodeConfigValidator.validateNodeConfigs(provider, instanceData, true);
    assertEquals(instanceData.nodeConfigs.size() + 1, results.keySet().size());
    validCount = results.values().stream().filter(r -> r.isValid()).count();
    assertEquals(8, validCount);
    result = results.get(Type.PROMETHEUS_NO_NODE_EXPORTER);
    assertTrue(result.isValid());
    assertTrue(result.isRequired());
  }
}
