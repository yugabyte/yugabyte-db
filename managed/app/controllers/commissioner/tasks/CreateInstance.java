// Copyright (c) Yugabyte, Inc.

package controllers.commissioner.tasks;

import java.util.Vector;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.util.concurrent.ThreadFactoryBuilder;

import controllers.commissioner.ITask;
import controllers.commissioner.TaskList;
import forms.commissioner.CreateInstanceTaskParams;
import forms.commissioner.ITaskParams;
import play.libs.Json;

public class CreateInstance implements ITask {

  public static final Logger LOG = LoggerFactory.getLogger(CreateInstance.class);

  // The task params.
  CreateInstanceTaskParams taskParams;

  @Override
  public void initialize(ITaskParams taskParams) {
    this.taskParams = (CreateInstanceTaskParams)taskParams;
  }

  @Override
  public String getName() {
    // Debug log the subnets.
    String subnets = "";
    for (String subnet : taskParams.subnets) {
      subnets += subnet + " ";
    }
    return "CreateInstance(" + taskParams.instanceName + ")";
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  @Override
  public void run() {
    LOG.info("Started task.");
    try {
      // Create the master task list.
      Vector<TaskList> masterTaskList = new Vector<TaskList>();

      // Create the threadpool for the subtasks to use.
      int numThreads = 10;
      ThreadFactory namedThreadFactory =
          new ThreadFactoryBuilder().setNameFormat("TaskPool-" + getName() + "-%d").build();
      ExecutorService executor = Executors.newFixedThreadPool(numThreads, namedThreadFactory);
      LOG.info("Started TaskPool with " + numThreads + " threads.");

      // Persist information about the instance. such as the set of nodes we are going to provision,
      // what services run on each of them (master, tserver, etc), placement information, etc.

      // Create the required number of nodes in the appropriate locations.
      TaskList createServersTaskList = createTaskSetupServers(executor);
      masterTaskList.add(createServersTaskList);

      // Deploy the software on all the nodes.

      // Configure the instance by picking the masters appropriately.

      // Start the YB cluster.

      // Persist the placement info into the YB master.

      // Update the MetaMaster about the latest placement information.

      // Run all the tasks.
      for (TaskList taskList : masterTaskList) {
        taskList.run();
        taskList.waitFor();
      }
    } catch (Throwable t) {
      LOG.error("Error executing task " + getName(), t);
      throw t;
    }
    LOG.info("Finished task.");
  }

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("name : " + this.getClass().getName());
    return sb.toString();
  }

  public TaskList createTaskSetupServers(ExecutorService executor) {
    TaskList taskList = new TaskList(getName(), executor);
    for (int nodeIdx = 1; nodeIdx < taskParams.numNodes + 1; nodeIdx++) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      // Set the cloud name.
      params.cloud = AnsibleSetupServer.CloudType.aws;
      // Add the node name.
      params.nodeInstanceName = taskParams.instanceName + "-n" + nodeIdx;
      // VPC is one of subnet-6553f513 subnet-f840ce9c subnet-01ac5b59
      params.vpcId = taskParams.subnets.get(nodeIdx % taskParams.subnets.size());
      // Create the Ansible task to setup the server.
      AnsibleSetupServer ansibleSetupServer = new AnsibleSetupServer();
      ansibleSetupServer.initialize(params);
      // Add it to the task list.
      taskList.addTask(ansibleSetupServer);
    }
    return taskList;
  }
}
