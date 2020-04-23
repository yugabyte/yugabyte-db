/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.IsServerReadyResponse;
import org.yb.client.YBClient;
import org.yb.tserver.Tserver.TabletServerErrorPB;

import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;

import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.subtasks.ServerSubTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;

import java.util.Map;
import java.util.Map.Entry;

import play.api.Play;

public class SetFlagInMemory extends ServerSubTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(SetFlagInMemory.class);

  // The gflag for tserver addresses.
  private static final String TSERVER_MASTER_ADDR_FLAG = "tserver_master_addrs";

  // The gflag for master addresses.
  private static final String MASTER_MASTER_ADDR_FLAG = "master_addresses";

  // Parameters for wait task.
  public static class Params extends ServerSubTaskParams {
    // If the flags need to be forced to update (if it is not marked runtime safe).
    public boolean force = false;

    // The set of flags that need to be updated.
    public Map<String, String> gflags;

    // If ONLY the masters need to be updated.
    public boolean updateMasterAddrs = false;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    checkParams();
    String masterAddresses = getMasterAddresses();
    YBClient client = getClient();
    boolean isTserverTask = taskParams().serverType == ServerType.TSERVER;
    HostAndPort hp = getHostPort();

    Map<String, String> gflags = taskParams().gflags;
    if (taskParams().updateMasterAddrs) {
      String flagToSet = isTserverTask ? TSERVER_MASTER_ADDR_FLAG : MASTER_MASTER_ADDR_FLAG;
      gflags = ImmutableMap.of(flagToSet, masterAddresses);
    }
    try {
      if (gflags == null) {
        throw new IllegalArgumentException("Gflags cannot be null during a setFlag operation.");
      }
      for (Entry<String, String> gflag: gflags.entrySet()) {
        boolean setSuccess = client.setFlag(hp, gflag.getKey(), gflag.getValue(),
                                            taskParams().force);
        if (!setSuccess) {
          throw new RuntimeException("Could not set gflag " + gflag + " for " +
                                     taskParams().serverType + " on node " + taskParams().nodeName);
        }
      }
    } catch (Exception e) {
      LOG.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
    closeClient(client);
  }
}
