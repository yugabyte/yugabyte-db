// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.common;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.forms.HierarchicalNodesSpec;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;

public class SpecificationProviderInitializer implements ProviderInitializer {
  private final UserIntent userIntent;
  private final UniverseDefinitionTaskParams.ProviderSpecification providerSpecification;

  public SpecificationProviderInitializer(UserIntent userIntent, UUID providerUUID) {
    this.userIntent = userIntent;
    providerSpecification = userIntent.getOrCreateProviderSpecification(providerUUID);
    if (providerSpecification.getNodesSpecs() == null) {
      providerSpecification.setNodesSpecs(
          HierarchicalNodesSpec.RootNodesSpec.builder()
              .tserverSpecification(HierarchicalNodesSpec.NodeSpec.empty())
              .build());
    }
  }

  @Override
  public ProviderInitializer setProviderUUID(UUID providerUUID) {
    providerSpecification.setProviderUUID(providerUUID);
    return this;
  }

  @Override
  public ProviderInitializer setProviderType(CloudType cloudType) {
    providerSpecification.setProviderType(cloudType);
    return this;
  }

  @Override
  public ProviderInitializer setInstanceType(String instanceType) {
    providerSpecification.getNodesSpecs().getOrCreateTserverSpec().setInstanceType(instanceType);
    return this;
  }

  @Override
  public ProviderInitializer setMasterInstanceType(String masterInstanceType) {
    providerSpecification
        .getNodesSpecs()
        .getOrCreateMasterSpec()
        .setInstanceType(masterInstanceType);
    return this;
  }

  @Override
  public ProviderInitializer setDeviceInfo(DeviceInfo deviceInfo) {
    providerSpecification.getNodesSpecs().getOrCreateTserverSpec().setDeviceInfo(deviceInfo);
    return this;
  }

  @Override
  public ProviderInitializer setMasterDeviceInfo(DeviceInfo masterDeviceInfo) {
    providerSpecification.getNodesSpecs().getOrCreateMasterSpec().setDeviceInfo(masterDeviceInfo);
    return this;
  }

  @Override
  public ProviderInitializer setAccessCode(String accessKeyCode) {
    providerSpecification.setAccessKeyCode(accessKeyCode);
    return this;
  }

  @Override
  public ProviderInitializer setInstanceTags(Map<String, String> instanceTags) {
    providerSpecification.setInstanceTags(instanceTags);
    return this;
  }

  @Override
  public ProviderInitializer setImageBundleUUID(UUID imageBundleUUID) {
    providerSpecification.setImageBundleUUID(imageBundleUUID);
    return this;
  }

  @Override
  public ProviderInitializer setCGroupSize(Integer cGroupSize) {
    providerSpecification.getNodesSpecs().getOrCreateTserverSpec().setCgroupSize(cGroupSize);
    return this;
  }

  @Override
  public ProviderInitializer updateDeviceInfo(Consumer<DeviceInfo> mutator) {
    HierarchicalNodesSpec.NodeSpec tserverSpec =
        providerSpecification.getNodesSpecs().getOrCreateTserverSpec();
    if (tserverSpec.getDeviceInfo() == null) {
      tserverSpec.setDeviceInfo(new DeviceInfo());
    }
    mutator.accept(tserverSpec.getDeviceInfo());
    return this;
  }

  @Override
  public ProviderInitializer updateMasterDeviceInfo(Consumer<DeviceInfo> mutator) {
    HierarchicalNodesSpec.NodeSpec masterSpec =
        providerSpecification.getNodesSpecs().getOrCreateMasterSpec();
    if (masterSpec.getDeviceInfo() == null) {
      masterSpec.setDeviceInfo(new DeviceInfo());
    }
    mutator.accept(masterSpec.getDeviceInfo());
    return this;
  }

  @Override
  public UserIntent getUserIntent() {
    return userIntent;
  }

  @Override
  public UUID getCurrentProviderUUID() {
    return providerSpecification.getProviderUUID();
  }
}
