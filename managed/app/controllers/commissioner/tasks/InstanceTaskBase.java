// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.net.HostAndPort;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import java.util.List;
import java.util.Vector;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;
import play.libs.Json;

import controllers.commissioner.AbstractTaskBase;
import controllers.commissioner.Common.CloudType;
import controllers.commissioner.ITask;
import controllers.commissioner.TaskList;
import controllers.commissioner.TaskListQueue;
import forms.commissioner.InstanceTaskParams;
import forms.commissioner.ITaskParams;
import models.commissioner.InstanceInfo;
import models.commissioner.InstanceInfo.NodeDetails;
import org.yb.client.MiniYBCluster;
import services.YBMiniClusterService;
import util.Util;

public abstract class InstanceTaskBase extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(InstanceTaskBase.class);

  public static final boolean isLocalTesting = Util.isLocalTesting();
  // Only used for local testing, maintained here to control creation and deletion.
  public static MiniYBCluster miniCluster = null;

  @Override 
  public void initialize(ITaskParams params) {
    this.taskParams = (InstanceTaskParams)params;
    if (isLocalTesting) {
      this.miniCluster = Play.current().injector().instanceOf(YBMiniClusterService.class)
          .getMiniCluster(this.taskParams.numNodes);
    }
    // Create the threadpool for the subtasks to use.
    int numThreads = 10;
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("TaskPool-" + getName() + "-%d").build();
    executor = Executors.newFixedThreadPool(numThreads, namedThreadFactory);
  }

  // The task params.
  InstanceTaskParams taskParams;

  // The threadpool on which the tasks are executed.
  ExecutorService executor;

  // The sequence of task lists that should be executed.
  TaskListQueue taskListQueue;

  @Override
  public abstract void run();

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  @Override
  public int getPercentCompleted() {
    if (taskListQueue == null) {
      return 0;
    }

    return taskListQueue.getPercentCompleted();
  }

  @Override
  public void finalize() {
    if (isLocalTesting && miniCluster != null) {
      Util.closeMiniCluster(miniCluster);
      miniCluster = null;
    }
  }

  public TaskList createTaskListToSetupServers() {
    return createTaskListToSetupServers(1);
  }

  public TaskList createTaskListToSetupServers(int startIndex) {
    TaskList taskList = new TaskList("AnsibleSetupServer", executor);
    for (int nodeIdx = startIndex; nodeIdx < startIndex + taskParams.numNodes; nodeIdx++) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Add the node name.
      params.nodeInstanceName = taskParams.instanceName + "-n" + nodeIdx;
      // Pick one of the VPCs in a round robin fashion.
      params.vpcId = taskParams.subnets.get(nodeIdx % taskParams.subnets.size());
      // Create the Ansible task to setup the server.
      AnsibleSetupServer ansibleSetupServer = TaskUtil.newAnsibleSetupServer();
      ansibleSetupServer.initialize(params);
      // Add it to the task list.
      taskList.addTask(ansibleSetupServer);
    }
    return taskList;
  }

  public TaskList createTaskListToGetServerInfo() {
    return createTaskListToGetServerInfo(1);
  }

  public TaskList createTaskListToGetServerInfo(int startIndex) {
    TaskList taskList = new TaskList("AnsibleUpdateNodeInfo", executor);
    for (int nodeIdx = startIndex; nodeIdx < startIndex + taskParams.numNodes; nodeIdx++) {
      AnsibleUpdateNodeInfo.Params params = new AnsibleUpdateNodeInfo.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Add the node name.
      params.nodeInstanceName = taskParams.instanceName + "-n" + nodeIdx;
      // Add the instance uuid.
      params.instanceUUID = taskParams.instanceUUID;
      params.isCreateInstance = taskParams.create;
      params._local_test_subnets = taskParams.subnets;
      // Create the Ansible task to get the server info.
      AnsibleUpdateNodeInfo ansibleFindCloudHost = TaskUtil.newAnsibleUpdateNodeInfo();
      ansibleFindCloudHost.initialize(params);
      // Add it to the task list.
      taskList.addTask(ansibleFindCloudHost);
    }
    return taskList;
  }

  public TaskList createTaskListToCreateClusterConf() {
    return createTaskListToCreateClusterConf(0);
  }

  public TaskList createTaskListToCreateClusterConf(int numMasters) {
    TaskList taskList = new TaskList("CreateClusterConf", executor);
    CreateClusterConf.Params params = new CreateClusterConf.Params();
    // Set the cloud name.
    params.cloud = CloudType.aws;
    // Add the instance uuid.
    params.instanceUUID = taskParams.instanceUUID;

    // For edit instance case, the number of masters need to be same as existing universe.
    if (!taskParams.create) {
      params.numMastersToChoose = numMasters;
    }
    params.isCreateInstance = taskParams.create;
    // Create the task.
    CreateClusterConf task = new CreateClusterConf();
    task.initialize(params);
    // Add it to the task list.
    taskList.addTask(task);
    return taskList;
  }

  public TaskList createTaskListToConfigureServers() {
    return createTaskListToConfigureServers(1);
  }

  public TaskList createTaskListToConfigureServers(int startIndex) {
    TaskList taskList = new TaskList("AnsibleConfigureServers", executor);
    for (int nodeIdx = startIndex; nodeIdx < startIndex + taskParams.numNodes; nodeIdx++) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Add the node name.
      params.nodeInstanceName = taskParams.instanceName + "-n" + nodeIdx;
      // Add the instance uuid.
      params.instanceUUID = taskParams.instanceUUID;
      // The software package to install for this cluster.
      params.ybServerPkg = taskParams.ybServerPkg;
      // Create the Ansible task to get the server info.
      AnsibleConfigureServers task = TaskUtil.newAnsibleConfigureServers();
      task.initialize(params);
      // Add it to the task list.
      taskList.addTask(task);
    }
    return taskList;
  }

  public TaskList createTaskListToCreateCluster() {
    return createTaskListToCreateCluster(false);
  }
  public TaskList createTaskListToCreateCluster(boolean isShell) {
    TaskList taskList = new TaskList("AnsibleClusterServerCtl", executor);
    List<NodeDetails> masters = isShell ? InstanceInfo.getNewMasters(taskParams.instanceUUID)
                                        : InstanceInfo.getMasters(taskParams.instanceUUID);
    for (NodeDetails node : masters) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Add the node name.
      params.nodeInstanceName = node.instance_name;
      // Add the instance uuid.
      params.instanceUUID = taskParams.instanceUUID;
      // The service and the command we want to run.
      params.process = "master";
      params.command = "create";
      params.isShell = isShell;
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = TaskUtil.newAnsibleClusterServerCtl();
      task.initialize(params);
      // Add it to the task list.
      taskList.addTask(task);
    }
    return taskList;
  }

  public TaskList createTaskListToStartTServers() {
    return createTaskListToStartTServers(false);
  }
  public TaskList createTaskListToStartTServers(boolean isShell) {
    TaskList taskList = new TaskList("AnsibleClusterServerCtl", executor);
    List<NodeDetails> tservers = isShell ? InstanceInfo.getNewMasters(taskParams.instanceUUID)
                                         : InstanceInfo.getTServers(taskParams.instanceUUID);
    for (NodeDetails node : tservers) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      // Set the cloud name.
      params.cloud = CloudType.aws;
      // Add the node name.
      params.nodeInstanceName = node.instance_name;
      // Add the instance uuid.
      params.instanceUUID = taskParams.instanceUUID;
      // The service and the command we want to run.
      params.process = "tserver";
      params.command = "start";
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = TaskUtil.newAnsibleClusterServerCtl();
      task.initialize(params);
      // Add it to the task list.
      taskList.addTask(task);
    }
    return taskList;
  }
}
