// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.XClusterConfig;
import java.util.UUID;
import lombok.NoArgsConstructor;

@NoArgsConstructor
public class XClusterConfigTaskParams extends UniverseDefinitionTaskParams {

  public XClusterConfig xClusterConfig;
  public XClusterConfigCreateFormData createFormData;
  public XClusterConfigEditFormData editFormData;

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig, XClusterConfigCreateFormData createFormData) {
    this.universeUUID = xClusterConfig.targetUniverseUUID;
    this.xClusterConfig = xClusterConfig;
    this.createFormData = createFormData;
  }

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig, XClusterConfigEditFormData editFormData) {
    this.universeUUID = xClusterConfig.targetUniverseUUID;
    this.xClusterConfig = xClusterConfig;
    this.editFormData = editFormData;
  }

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig, XClusterConfigRestartFormData restartFormData) {
    this.universeUUID = xClusterConfig.targetUniverseUUID;
    this.xClusterConfig = xClusterConfig;
    this.createFormData = new XClusterConfigCreateFormData();
    this.createFormData.tables = restartFormData.tables;
    this.createFormData.bootstrapParams = new XClusterConfigCreateFormData.BootstrapParams();
    this.createFormData.bootstrapParams.backupRequestParams =
        restartFormData.bootstrapParams.backupRequestParams;
  }

  public XClusterConfigTaskParams(XClusterConfig xClusterConfig) {
    this.universeUUID = xClusterConfig.targetUniverseUUID;
    this.xClusterConfig = xClusterConfig;
  }

  public XClusterConfigTaskParams(UUID targetUniverseUUID) {
    this.universeUUID = targetUniverseUUID;
  }
}
