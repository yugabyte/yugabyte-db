// Copyright (c) YugaByte, Inc.

package org.yb.client;

import java.util.List;

import org.yb.annotations.InterfaceAudience;
import org.yb.Common.HostPortPB;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.master.Master;


@InterfaceAudience.Public
public abstract class AbstractModifyMasterClusterConfig {
  private YBClient ybClient = null;

  public AbstractModifyMasterClusterConfig(YBClient client) {
    ybClient = client;
  }

  public String doCall() throws Exception {
    GetMasterClusterConfigResponse getResponse = ybClient.getMasterClusterConfig();
    if (getResponse.hasError()) {
      return "Hit error: " + getResponse.errorMessage();
    }
    Master.SysClusterConfigEntryPB newConfig = modifyConfig(getResponse.getConfig());
    ChangeMasterClusterConfigResponse changeResp = ybClient.changeMasterClusterConfig(newConfig);
    if (changeResp.hasError()) {
      return "Hit error: " + changeResp.errorMessage();
    }

    // Get the current cluster config.
    return "Updated config: " + newConfig;
  }

  abstract protected Master.SysClusterConfigEntryPB modifyConfig(Master.SysClusterConfigEntryPB config);
}
