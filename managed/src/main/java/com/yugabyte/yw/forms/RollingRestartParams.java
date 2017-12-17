package com.yugabyte.yw.forms;

import java.util.HashMap;
import java.util.Map;
import java.util.UUID;


import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse;
import play.data.validation.Constraints;

public class RollingRestartParams extends UniverseDefinitionTaskParams {

  // The universe that we want to perform a rolling restart on.
  @Constraints.Required()
  public UUID universeUUID;

  // Rolling Restart task type
  @Constraints.Required()
  public UpgradeUniverse.UpgradeTaskType taskType;

  // The software version to install. Do not set this value if no software needs to be installed.
  public String ybSoftwareVersion = null;

  @Deprecated
  // This is deprecated use cluster.userIntent.masterGFlags
  public Map<String, String> masterGFlags = new HashMap<String, String>();
  @Deprecated
  // This is deprecated use cluster.userIntent.tserverGFlags
  public Map<String, String> tserverGFlags = new HashMap<String, String>();

  public Integer sleepAfterMasterRestartMillis = 180000;
  public Integer sleepAfterTServerRestartMillis = 180000;

  public Boolean rollingUpgrade = true;
}
