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

  public static final int DEFAULT_SLEEP_AFTER_RESTART_MS = 240000;

  // Time to sleep after a server restart, if there is no way to check/rpc if it is ready to server requests.
  public Integer sleepAfterMasterRestartMillis = DEFAULT_SLEEP_AFTER_RESTART_MS;
  public Integer sleepAfterTServerRestartMillis = DEFAULT_SLEEP_AFTER_RESTART_MS;

  public Boolean rollingUpgrade = true;
}
