// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.TaskListQueue;
import models.commissioner.InstanceInfo;

public class CreateInstance extends InstanceTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CreateInstance.class);

  @Override
  public String toString() {
    return getName() + "(" + taskParams.instanceUUID + ")";
  }

  @Override
  public String getName() {
    return "CreateInstance";
  }

  @Override
  public void run() {
    LOG.info("Started {} task.", getName());
    try {
      // Create the task list sequence.
      taskListQueue = new TaskListQueue();

      // Persist information about the instance.
      InstanceInfo.createInstance(taskParams.instanceUUID,
                                  taskParams.subnets,
                                  taskParams.numNodes,
                                  taskParams.ybServerPkg);

      // Create the required number of nodes in the appropriate locations.
      taskListQueue.add(createTaskListToSetupServers(1 /* startIndex */));

      // Get all information about the nodes of the cluster. This includes the public ip address,
      // the private ip address (in the case of AWS), etc.
      taskListQueue.add(createTaskListToGetServerInfo(1 /* startIndex */));

      // Pick the masters and persist the plan in the middleware.
      taskListQueue.add(createTaskListToCreateClusterConf(0 /* mastersToChoose */));

      // Configures and deploys software on all the nodes (masters and tservers).
      taskListQueue.add(createTaskListToConfigureServers(1 /* startIndex */));

      // Creates the YB cluster by starting the masters in the create mode.
      taskListQueue.add(createTaskListToCreateCluster(false /* isShell */));

      // Persist the placement info into the YB master.
      taskListQueue.add(createPlacementInfoTask(false /* isShell */));

      // Start the tservers in the clusters.
      taskListQueue.add(createTaskListToStartTServers(false /* isEdit */));

      // TODO: Update the MetaMaster about the latest placement information.

      // Run all the tasks.
      taskListQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {}, error={}", getName(), t);
      throw t;
    }
    LOG.info("Finished {} task.", getName());
  }
}
