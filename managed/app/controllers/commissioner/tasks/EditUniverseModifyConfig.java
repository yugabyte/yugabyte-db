// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import java.util.UUID;

import org.yb.annotations.InterfaceAudience;
import org.yb.client.AbstractModifyMasterClusterConfig;
import org.yb.client.YBClient;
import org.yb.master.Master;

@InterfaceAudience.Public
public class EditUniverseModifyConfig extends AbstractModifyMasterClusterConfig {
  UUID instanceUUID;

  public EditUniverseModifyConfig(YBClient client, UUID instanceUUID) {
    super(client);
    this.instanceUUID = instanceUUID;
  }

  @Override
  protected Master.SysClusterConfigEntryPB modifyConfig(Master.SysClusterConfigEntryPB config) {
    Master.SysClusterConfigEntryPB.Builder configBuilder =
        Master.SysClusterConfigEntryPB.newBuilder(config);
    // TODO: Set the final placement info and the blacklist of server's from the old
    // config and call YB Change Master Cluster api.

    return configBuilder.build();
  }
}
