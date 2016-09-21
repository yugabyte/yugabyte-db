// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.UniverseDetails;

public class AnsibleDestroyServer extends NodeTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(AnsibleDestroyServer.class);

  // Params for this task.
  public static class Params extends NodeTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  private void removeNodeFromUniverse(String nodeName) {
    // Persist the desired node information into the DB.
    UniverseUpdater updater = new UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDetails universeDetails = universe.getUniverseDetails();
        universeDetails.nodeDetailsMap.remove(nodeName);
        LOG.debug("Removing node " + nodeName +
                  " from universe " + taskParams().universeUUID);
      }
    };

    Universe.saveDetails(taskParams().universeUUID, updater);
  }

  @Override
  public void run() {
    String aws_vpc_subnet_id = AvailabilityZone.find.byId(taskParams().azUuid).subnet;
    String command = "yb_server_provision.py " + taskParams().nodeName +
                     " --cloud " + taskParams().cloud +
                     " --region " + taskParams().getRegion().code +
                     " --aws_vpc_subnet_id " + aws_vpc_subnet_id +
                     " --destroy";

    // Execute the ansible command.
    execCommand(command);

    removeNodeFromUniverse(taskParams().nodeName);
  }
}
