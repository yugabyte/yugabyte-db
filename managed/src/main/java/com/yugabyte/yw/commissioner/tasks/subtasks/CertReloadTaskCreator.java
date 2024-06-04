package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.IUpgradeSubTask;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.subtasks.NodeCertReloadTask.Params;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Arrays;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

/**
 * CertReloadTaskCreator creates and adds tasks to the subgroup It may not be ideal for this class
 * to add tasks in addition to creating it, however, this is as per the framework
 *
 * @author msoundar
 */
@Slf4j
public class CertReloadTaskCreator implements IUpgradeSubTask {

  private final String masterHostPorts;
  private final UUID userTaskUuid;
  private final UUID universeUuid;
  private final RunnableTask runnableTask;
  private final TaskExecutor taskExecutor;

  public CertReloadTaskCreator(
      UUID universeId,
      UUID userTaskId,
      RunnableTask runnableTask,
      TaskExecutor taskExecutor,
      List<NodeDetails> masterNodes) {

    if (taskExecutor == null || masterNodes == null || masterNodes.isEmpty()) {
      throw new IllegalArgumentException("TaskGroup or MasterNodes cannot be empty");
    }
    this.universeUuid = universeId;
    this.userTaskUuid = userTaskId;
    this.taskExecutor = taskExecutor;
    this.runnableTask = runnableTask;
    List<String> masterHostPortList =
        masterNodes.stream()
            .map(n -> getHostPort(getHost(n), getMasterPort(n)))
            .collect(Collectors.toList());
    this.masterHostPorts = String.join(",", masterHostPortList);
  }

  /**
   * The entry point to reload the certificates
   *
   * @param nodes list of node details to reload the cert. This need not include all the nodes in
   *     the universe.
   * @param processTypes list of process types running on the above nodes.
   * @throws RuntimeException
   */
  public void create(List<NodeDetails> nodes, Set<ServerType> processTypes)
      throws RuntimeException {

    if (nodes == null || nodes.isEmpty()) return;

    log.debug("received request to reload cert for {} of type {}", nodes, processTypes);
    if (processTypes == null
        || processTypes.isEmpty()
        || (!processTypes.contains(ServerType.MASTER)
            && !processTypes.contains(ServerType.TSERVER))) {
      log.warn("process type cannot be empty and should be either of MASTER or TSERFVER");
      return;
    }

    SubTaskGroup subTaskGroup =
        this.taskExecutor.createSubTaskGroup(
            "CertReloadTask_" + Arrays.toString(processTypes.toArray(new ServerType[0])));

    nodes.forEach(
        node -> {
          String nodeHostPort = getHostPort(node, processTypes);
          NodeCertReloadTask task = NodeCertReloadTask.createTask();
          Params params = new Params();
          params.setUniverseUUID(universeUuid);
          params.azUuid = node.azUuid;
          params.nodeName = node.nodeName;
          params.setMasters(masterHostPorts);
          params.setNode(nodeHostPort);
          task.initialize(params);
          task.setUserTaskUUID(userTaskUuid);
          subTaskGroup.addSubTask(task);
        });
    if (subTaskGroup.getSubTaskCount() > 0) {
      this.runnableTask.addSubTaskGroup(subTaskGroup);
    }
  }

  private String getHostPort(NodeDetails node, Set<ServerType> processTypes) {
    String host = getHost(node);
    if (node.isTserver && processTypes != null && processTypes.contains(ServerType.TSERVER)) {
      String tserverHostPort = getHostPort(host, getTserverPort(node));
      return tserverHostPort;
    }
    if (node.isMaster && processTypes != null && processTypes.contains(ServerType.MASTER)) {
      String masterHostPort = getHostPort(host, getMasterPort(node));
      return masterHostPort;
    }
    return null;
  }

  private int getMasterPort(NodeDetails node) {
    return node.masterRpcPort;
  }

  private int getTserverPort(NodeDetails node) {
    return node.tserverRpcPort;
  }

  private String getHost(NodeDetails node) {
    return node.cloudInfo.private_ip;
  }

  private String getHostPort(String host, int port) {
    return String.format("%s:%d", host, port);
  }

  @Override
  public void run(List<NodeDetails> nodes, Set<ServerType> processTypes) {
    create(nodes, processTypes);
  }
}
