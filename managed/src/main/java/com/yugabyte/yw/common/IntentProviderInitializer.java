// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.common;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;

public class IntentProviderInitializer implements ProviderInitializer {
  private final UserIntent userIntent;

  public IntentProviderInitializer(UserIntent userIntent, UUID providerUUID) {
    this.userIntent = userIntent;
    userIntent.provider = providerUUID.toString();
  }

  @Override
  public ProviderInitializer setProviderUUID(UUID providerUUID) {
    userIntent.provider = providerUUID.toString();
    return this;
  }

  @Override
  public ProviderInitializer setProviderType(CloudType cloudType) {
    userIntent.providerType = cloudType;
    return this;
  }

  @Override
  public ProviderInitializer setInstanceType(String instanceType) {
    userIntent.instanceType = instanceType;
    return this;
  }

  @Override
  public ProviderInitializer setMasterInstanceType(String masterInstanceType) {
    userIntent.masterInstanceType = masterInstanceType;
    return this;
  }

  @Override
  public ProviderInitializer setDeviceInfo(DeviceInfo deviceInfo) {
    userIntent.deviceInfo = deviceInfo;
    return this;
  }

  @Override
  public ProviderInitializer setMasterDeviceInfo(DeviceInfo masterDeviceInfo) {
    userIntent.masterDeviceInfo = masterDeviceInfo;
    return this;
  }

  @Override
  public ProviderInitializer setAccessCode(String accessKeyCode) {
    userIntent.accessKeyCode = accessKeyCode;
    return this;
  }

  @Override
  public ProviderInitializer setInstanceTags(Map<String, String> instanceTags) {
    userIntent.instanceTags = instanceTags;
    return this;
  }

  @Override
  public ProviderInitializer setImageBundleUUID(UUID imageBundleUUID) {
    userIntent.imageBundleUUID = imageBundleUUID;
    return this;
  }

  @Override
  public ProviderInitializer setCGroupSize(Integer cGroupSize) {
    userIntent.setCgroupSize(cGroupSize);
    return this;
  }

  @Override
  public ProviderInitializer updateDeviceInfo(Consumer<DeviceInfo> mutator) {
    if (userIntent.deviceInfo == null) {
      userIntent.deviceInfo = new DeviceInfo();
    }
    mutator.accept(userIntent.deviceInfo);
    return this;
  }

  @Override
  public ProviderInitializer updateMasterDeviceInfo(Consumer<DeviceInfo> mutator) {
    if (userIntent.masterDeviceInfo == null) {
      userIntent.masterDeviceInfo = new DeviceInfo();
    }
    mutator.accept(userIntent.masterDeviceInfo);
    return this;
  }

  @Override
  public UserIntent getUserIntent() {
    return userIntent;
  }

  @Override
  public UUID getCurrentProviderUUID() {
    return UUID.fromString(userIntent.provider);
  }
}
