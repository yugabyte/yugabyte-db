// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.XClusterConfig;
import java.util.UUID;

public class XClusterConfigTaskParams extends UniverseTaskParams {

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

  public XClusterConfigTaskParams(XClusterConfig xClusterConfig) {
    this.universeUUID = xClusterConfig.targetUniverseUUID;
    this.xClusterConfig = xClusterConfig;
  }

  public XClusterConfigTaskParams(UUID targetUniverseUUID) {
    this.universeUUID = targetUniverseUUID;
  }
}
