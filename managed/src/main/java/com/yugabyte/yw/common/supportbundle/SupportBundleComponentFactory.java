package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;

@Singleton
public class SupportBundleComponentFactory {

  private final ApplicationLogsComponent applicationLogsComponent;
  private final UniverseLogsComponent universeLogsComponent;
  private final OutputFilesComponent outputFilesComponent;
  private final ErrorFilesComponent errorFilesComponent;
  private final GFlagsComponent gFlagsComponent;
  private final InstanceComponent instanceComponent;
  private final ConsensusMetaComponent consensusMetaComponent;
  private final TabletMetaComponent tabletMetaComponent;
  private final YbcLogsComponent ybcLogsComponent;

  @Inject
  public SupportBundleComponentFactory(
      ApplicationLogsComponent applicationLogsComponent,
      UniverseLogsComponent universeLogsComponent,
      OutputFilesComponent outputFilesComponent,
      ErrorFilesComponent errorFilesComponent,
      GFlagsComponent gFlagsComponent,
      InstanceComponent instanceComponent,
      ConsensusMetaComponent consensusMetaComponent,
      TabletMetaComponent tabletMetaComponent,
      YbcLogsComponent ybcLogsComponent) {
    this.applicationLogsComponent = applicationLogsComponent;
    this.universeLogsComponent = universeLogsComponent;
    this.outputFilesComponent = outputFilesComponent;
    this.errorFilesComponent = errorFilesComponent;
    this.gFlagsComponent = gFlagsComponent;
    this.instanceComponent = instanceComponent;
    this.consensusMetaComponent = consensusMetaComponent;
    this.tabletMetaComponent = tabletMetaComponent;
    this.ybcLogsComponent = ybcLogsComponent;
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
      case OutputFiles:
        supportBundleComponent = this.outputFilesComponent;
        break;
      case ErrorFiles:
        supportBundleComponent = this.errorFilesComponent;
        break;
      case GFlags:
        supportBundleComponent = this.gFlagsComponent;
        break;
      case Instance:
        supportBundleComponent = this.instanceComponent;
        break;
      case ConsensusMeta:
        supportBundleComponent = this.consensusMetaComponent;
        break;
      case TabletMeta:
        supportBundleComponent = this.tabletMetaComponent;
        break;
      case YbcLogs:
        supportBundleComponent = this.ybcLogsComponent;
        break;
      default:
        break;
    }

    return supportBundleComponent;
  }
}
