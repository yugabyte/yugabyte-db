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

  public void doCall() throws Exception {
    GetMasterClusterConfigResponse getResponse = ybClient.getMasterClusterConfig();
    if (getResponse.hasError()) {
      throw new RuntimeException("Get config hit error: " + getResponse.errorMessage());
    }
    Master.SysClusterConfigEntryPB newConfig = modifyConfig(getResponse.getConfig());
    ChangeMasterClusterConfigResponse changeResp = ybClient.changeMasterClusterConfig(newConfig);
    if (changeResp.hasError()) {
      throw new RuntimeException("ChangeConfig hit error: " + changeResp.errorMessage());
    }
  }

  abstract protected Master.SysClusterConfigEntryPB modifyConfig(Master.SysClusterConfigEntryPB config);
}
