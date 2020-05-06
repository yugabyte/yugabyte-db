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
import org.yb.client.YBClient;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.ServerSubTaskBase;

import play.api.Play;

public class WaitForServer extends ServerSubTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(WaitForServer.class);

  // Timeout for failing to respond to pings.
  private static final long TIMEOUT_SERVER_WAIT_MS = 120000;

  public static class Params extends ServerSubTaskParams {
    // Timeout for the RPC call.
    public long serverWaitTimeoutMs = TIMEOUT_SERVER_WAIT_MS;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {

    checkParams();

    boolean ret = false;
    YBClient client = null;
    try {
      HostAndPort hp = getHostPort();
      client = getClient();

      ret = client.waitForServer(hp, taskParams().serverWaitTimeoutMs);
    } catch (Exception e) {
      LOG.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      closeClient(client);
    }
    if (!ret) {
      throw new RuntimeException(getName() + " did not respond to pings in the set time.");
    }
  }
}
