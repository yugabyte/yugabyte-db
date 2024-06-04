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

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ManipulateDnsRecordTask extends UniverseTaskBase {

  private final DnsManager dnsManager;

  @Inject
  protected ManipulateDnsRecordTask(
      BaseTaskDependencies baseTaskDependencies, DnsManager dnsManager) {
    super(baseTaskDependencies);
    this.dnsManager = dnsManager;
  }

  public static class Params extends UniverseTaskParams {
    public DnsManager.DnsCommandType type;
    public UUID providerUUID;
    public String hostedZoneId;
    public String domainNamePrefix;
    public Boolean isForceDelete;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      List<NodeDetails> tserverNodes =
          Universe.getOrBadRequest(taskParams().getUniverseUUID()).getTServers();
      String nodeIpCsv =
          tserverNodes.stream().map(nd -> nd.cloudInfo.private_ip).collect(Collectors.joining(","));
      // Create the process to fetch information about the node from the cloud provider.
      dnsManager
          .manipulateDnsRecord(
              taskParams().type,
              taskParams().providerUUID,
              taskParams().hostedZoneId,
              taskParams().domainNamePrefix,
              nodeIpCsv)
          .processErrors();
    } catch (Exception e) {
      if (taskParams().type != DnsManager.DnsCommandType.Delete || !taskParams().isForceDelete) {
        throw e;
      } else {
        log.info(
            "Ignoring error in dns record deletion for {} due to isForceDelete being set.",
            taskParams().domainNamePrefix,
            e);
      }
    }
  }
}
