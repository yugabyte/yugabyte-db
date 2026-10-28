// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.common;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;

public interface ProviderInitializer {

  UserIntent getUserIntent();

  UUID getCurrentProviderUUID();

  ProviderInitializer setProviderUUID(UUID providerUUID);

  ProviderInitializer setProviderType(CloudType cloudType);

  ProviderInitializer setInstanceType(String instanceType);

  ProviderInitializer setMasterInstanceType(String masterInstanceType);

  ProviderInitializer setDeviceInfo(DeviceInfo deviceInfo);

  ProviderInitializer setMasterDeviceInfo(DeviceInfo masterDeviceInfo);

  ProviderInitializer setAccessCode(String accessKeyCode);

  ProviderInitializer setInstanceTags(Map<String, String> instanceTags);

  ProviderInitializer setImageBundleUUID(UUID imageBundleUUID);

  ProviderInitializer setCGroupSize(Integer cGroupSize);

  ProviderInitializer updateDeviceInfo(Consumer<DeviceInfo> mutator);

  ProviderInitializer updateMasterDeviceInfo(Consumer<DeviceInfo> mutator);
}
