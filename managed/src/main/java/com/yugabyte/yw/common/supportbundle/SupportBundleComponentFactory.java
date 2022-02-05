package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.common.supportbundle.ApplicationLogsComponent;
import com.yugabyte.yw.common.supportbundle.UniverseLogsComponent;

@Singleton
public class SupportBundleComponentFactory {

  private final ApplicationLogsComponent applicationLogsComponent;
  private final UniverseLogsComponent universeLogsComponent;

  @Inject
  public SupportBundleComponentFactory(
      ApplicationLogsComponent applicationLogsComponent,
      UniverseLogsComponent universeLogsComponent) {
    this.applicationLogsComponent = applicationLogsComponent;
    this.universeLogsComponent = universeLogsComponent;
  }

  // Maps the support bundle component type to its respective implementation
  public SupportBundleComponent getComponent(ComponentType componentType) {
    SupportBundleComponent supportBundleComponent = null;

    switch (componentType) {
      case UniverseLogs:
        supportBundleComponent = this.universeLogsComponent;
        break;
      case ApplicationLogs:
        supportBundleComponent = this.applicationLogsComponent;
        break;
      default:
        break;
    }

    return supportBundleComponent;
  }
}
