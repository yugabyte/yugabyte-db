package com.yugabyte.yw.forms;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import com.fasterxml.jackson.annotation.JsonIgnore;

import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse;
import play.data.validation.Constraints;

public class RollingRestartParams extends UniverseTaskParams {

  // TODO: try to switch to Map<String, String>
  public static class GFlag {
    public String name;
    public String value;
  }

  // The universe that we want to perform a rolling restart on.
  @Constraints.Required()
  public UUID universeUUID;

  // Rolling Restart task type
  @Constraints.Required()
  public UpgradeUniverse.UpgradeTaskType taskType;

  // The software version to install. Do not set this value if no software needs to be installed.
  public String ybSofwareVersion;

  // The new gflag values to update on the desired set of nodes.
  public List<GFlag> gflags;

  @JsonIgnore
  // Since we would use gflags as a map in task and subtask level
  public Map<String, String> getGFlagsAsMap() {
    Map<String, String> gflagsMap = new HashMap<>();
    for (GFlag gflag: gflags) {
      gflagsMap.put(gflag.name, gflag.value);
    }
    return gflagsMap;
  }

  // The nodes that we want to perform the operation and the subsequent rolling restart on.
  @Constraints.Required()
  public List<String> nodeNames;

  public Integer sleepAfterMasterRestartMillis = 180000;
  public Integer sleepAfterTServerRestartMillis = 180000;
  public Boolean upgradeMasters = true;
  public Boolean upgradeTServers = true;
}
