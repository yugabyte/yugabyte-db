package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.XClusterConfig;

public class XClusterConfigTaskParams extends AbstractTaskParams {

  public XClusterConfig xClusterConfig;
  public XClusterConfigCreateFormData createFormData;
  public XClusterConfigEditFormData editFormData;

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig, XClusterConfigCreateFormData createFormData) {
    this.xClusterConfig = xClusterConfig;
    this.createFormData = createFormData;
  }

  public XClusterConfigTaskParams(
      XClusterConfig xClusterConfig, XClusterConfigEditFormData editFormData) {
    this.xClusterConfig = xClusterConfig;
    this.editFormData = editFormData;
  }

  public XClusterConfigTaskParams(XClusterConfig xClusterConfig) {
    this.xClusterConfig = xClusterConfig;
  }
}
