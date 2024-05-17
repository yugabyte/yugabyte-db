// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doAnswer;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.BiConsumer;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.mockito.Mockito;

@Slf4j
public class MockUpgrade extends UpgradeTaskBase {

  private UpgradeContext context = DEFAULT_CONTEXT;
  private final UUID userTaskUUID = UUID.randomUUID();
  private final Map<Integer, List<TaskInfo>> subtasksByPosition = new HashMap<>();
  private final Universe universe;
  private final TaskExecutor.RunnableTask fakeRunnableTask;
  private final TaskExecutor.SubTaskGroup fakeSubTaskGroup;
  private Integer position = 0;
  private final List<TaskInfo> tasksForTaskGroup = new ArrayList<>();
  private final Set<Integer> positionsToSkipJsonCompare = new HashSet<>();

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return null;
  }

  @Override
  public UserTaskDetails.SubTaskGroupType getTaskSubGroupType() {
    return null;
  }

  @Override
  public NodeDetails.NodeState getNodeState() {
    return null;
  }

  @Override
  public UUID getUserTaskUUID() {
    return userTaskUUID;
  }

  @Override
  public void run() {}

  @Override
  protected void createPrecheckTasks(Universe universe) {}

  public void setUpgradeContext(UpgradeContext upgradeContext) {
    this.context = upgradeContext;
  }

  @Override
  protected UpgradeTaskParams taskParams() {
    UpgradeTaskParams params = new UpgradeTaskParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    return params;
  }

  @Override
  protected TaskExecutor.RunnableTask getRunnableTask() {
    return fakeRunnableTask;
  }

  public MockUpgrade(BaseTaskDependencies baseTaskDependencies, Universe universe) {
    super(baseTaskDependencies);

    this.universe = universe;

    fakeRunnableTask = Mockito.mock(TaskExecutor.RunnableTask.class);
    doAnswer(
            inv -> {
              flushSubtasks();
              return null;
            })
        .when(fakeRunnableTask)
        .addSubTaskGroup(any());

    fakeSubTaskGroup = Mockito.mock(TaskExecutor.SubTaskGroup.class);
    doAnswer(
            inv -> {
              ITask task = inv.getArgument(0);
              TaskType taskType =
                  Arrays.stream(TaskType.values())
                      .filter(tt -> tt.getTaskClass().equals(task.getClass()))
                      .findFirst()
                      .get();
              addTaskNoFlush(taskType, task.getTaskParams());
              positionsToSkipJsonCompare.add(position);
              return null;
            })
        .when(fakeSubTaskGroup)
        .addSubTask(any());
  }

  @Override
  protected TaskExecutor.SubTaskGroup createSubTaskGroup(
      String name, UserTaskDetails.SubTaskGroupType subTaskGroupType, boolean ignoreErrors) {
    return fakeSubTaskGroup;
  }

  private void addTaskNoFlush(TaskType taskType, JsonNode node) {
    TaskInfo taskInfo = new TaskInfo(taskType, null);
    taskInfo.setTaskParams(node);
    tasksForTaskGroup.add(taskInfo);
  }

  private void flushSubtasks() {
    subtasksByPosition.put(position++, new ArrayList<>(tasksForTaskGroup));
    tasksForTaskGroup.clear();
  }

  public MockUpgrade addTasks(TaskType... taskType) {
    for (TaskType type : taskType) {
      addTask(type, null);
    }
    return this;
  }

  public MockUpgrade addTask(TaskType taskType, JsonNode node) {
    addTaskNoFlush(taskType, node);
    flushSubtasks();
    return this;
  }

  public MockUpgrade precheckTasks(TaskType... taskTypes) {
    for (TaskType taskType : taskTypes) {
      addTask(taskType, null);
    }
    addTask(TaskType.FreezeUniverse, null);
    return this;
  }

  public UpgradeRound upgradeRound(UpgradeTaskParams.UpgradeOption upgradeOption) {
    return upgradeRound(upgradeOption, false);
  }

  public UpgradeRound upgradeRound(
      UpgradeTaskParams.UpgradeOption upgradeOption, boolean stopBothProcesses) {
    UpgradeRound upgradeRound = new UpgradeRound(upgradeOption, stopBothProcesses);
    return upgradeRound;
  }

  public class ExpectedTaskDetails {
    final TaskType taskType;
    final JsonNode details;
    final BiConsumer<JsonNode, NodeDetails> nodeDetailsCustomizer;
    final ServerType serverType;

    public ExpectedTaskDetails(
        TaskType taskType,
        JsonNode details,
        BiConsumer<JsonNode, NodeDetails> nodeDetailsCustomizer,
        ServerType serverType) {
      this.taskType = taskType;
      this.details = details;
      this.nodeDetailsCustomizer = nodeDetailsCustomizer;
      this.serverType = serverType;
    }

    @Override
    public String toString() {
      return "ScheduledTaskDetails{"
          + "taskType="
          + taskType
          + ", details="
          + details
          + ", serverType="
          + serverType
          + '}';
    }
  }

  public class UpgradeRound {
    private UpgradeTaskParams.UpgradeOption upgradeOption;
    private List<NodeDetails> masters = new ArrayList<>();
    private List<NodeDetails> tservers = new ArrayList<>();
    private boolean stopBothProcesses;
    private UpgradeContext context = MockUpgrade.this.context;
    private boolean ybcPresent = false;

    private List<ExpectedTaskDetails> expectedTasksList = new ArrayList<>();

    private UpgradeRound(UpgradeTaskParams.UpgradeOption upgradeOption, boolean stopBothProcesses) {
      this.upgradeOption = upgradeOption;
      this.stopBothProcesses = stopBothProcesses;
    }

    public UpgradeRound clusterNodes(UUID uuid) {
      tservers =
          fetchTServerNodes(upgradeOption).stream()
              .filter(n -> n.isInPlacement(uuid))
              .collect(Collectors.toList());
      masters =
          fetchMasterNodes(upgradeOption).stream()
              .filter(n -> n.isInPlacement(uuid))
              .collect(Collectors.toList());
      return this;
    }

    public UpgradeRound tservers() {
      tservers = fetchTServerNodes(upgradeOption);
      return this;
    }

    public UpgradeRound masters() {
      masters = fetchMasterNodes(upgradeOption);
      return this;
    }

    public UpgradeRound nodes(List<NodeDetails> masters, List<NodeDetails> tservers) {
      this.masters = new ArrayList<>(masters);
      this.tservers = new ArrayList<>(tservers);
      return this;
    }

    public UpgradeRound tserverTasks(TaskType... tasks) {
      for (TaskType task : tasks) {
        tserverTask(task);
      }
      return this;
    }

    public UpgradeRound tserverTask(TaskType task) {
      return tserverTask(task, null);
    }

    public UpgradeRound tserverTask(TaskType task, JsonNode details) {
      return tserverTask(task, details, null);
    }

    public UpgradeRound tserverTask(
        TaskType task, JsonNode details, BiConsumer<JsonNode, NodeDetails> nodeDetailsCustomizer) {
      return task(ServerType.TSERVER, task, details, nodeDetailsCustomizer);
    }

    public UpgradeRound masterTasks(TaskType... tasks) {
      for (TaskType task : tasks) {
        masterTask(task);
      }
      return this;
    }

    public UpgradeRound masterTask(TaskType task) {
      return masterTask(task, null);
    }

    public UpgradeRound masterTask(TaskType task, JsonNode details) {
      return masterTask(task, details, null);
    }

    public UpgradeRound masterTask(
        TaskType task, JsonNode details, BiConsumer<JsonNode, NodeDetails> nodeDetailsCustomizer) {
      return task(ServerType.MASTER, task, details, nodeDetailsCustomizer);
    }

    public UpgradeRound task(
        ServerType serverType,
        TaskType task,
        JsonNode details,
        BiConsumer<JsonNode, NodeDetails> nodeDetailsCustomizer) {
      expectedTasksList.add(
          new ExpectedTaskDetails(task, details, nodeDetailsCustomizer, serverType));
      return this;
    }

    public UpgradeRound withContext(UpgradeContext context) {
      this.context = context;
      return this;
    }

    public UpgradeRound withYbcPresent() {
      this.ybcPresent = true;
      return this;
    }

    public MockUpgrade applyRound() {
      if (masters.isEmpty() && tservers.isEmpty()) {
        // Using all nodes.
        masters();
        tservers();
      }
      switch (upgradeOption) {
        case ROLLING_UPGRADE:
          if (stopBothProcesses) {
            LinkedHashSet<NodeDetails> ls = new LinkedHashSet<>();
            ls.addAll(masters);
            ls.addAll(tservers);
            createRollingNodesUpgradeTaskFlow(this::applyUpgradeOnNodes, ls, context, ybcPresent);
          } else {
            createRollingUpgradeTaskFlow(
                this::applyUpgradeOnNodes,
                new MastersAndTservers(masters, tservers),
                context,
                ybcPresent);
          }
          break;
        case NON_ROLLING_UPGRADE:
          createNonRollingUpgradeTaskFlow(
              this::applyUpgradeOnNodes,
              new MastersAndTservers(masters, tservers),
              context,
              ybcPresent);
          break;
        case NON_RESTART_UPGRADE:
          if (stopBothProcesses) {
            createNonRestartUpgradeTaskFlow(
                this::applyUpgradeOnNodes, new MastersAndTservers(masters, tservers), context);
          } else {
            List<NodeDetails> nodes = new ArrayList<>(masters);
            nodes.addAll(tservers);
            createNonRestartUpgradeTaskFlow(
                this::applyUpgradeOnNodes, nodes, ServerType.EITHER, context);
          }
          break;
      }
      return MockUpgrade.this;
    }

    private void applyUpgradeOnNodes(List<NodeDetails> nodes, Set<ServerType> processTypes) {
      log.debug("Apply {} {}", processTypes, nodes);
      List<ExpectedTaskDetails> lst =
          expectedTasksList.stream()
              .filter(
                  t ->
                      processTypes.contains(t.serverType)
                          || processTypes.contains(ServerType.EITHER))
              .collect(Collectors.toList());
      for (ExpectedTaskDetails task : lst) {
        for (NodeDetails node : nodes) {
          JsonNode copy = null;
          if (task.details != null) {
            copy = task.details.deepCopy();
            if (task.nodeDetailsCustomizer != null) {
              task.nodeDetailsCustomizer.accept(copy, node);
            }
          }
          addTaskNoFlush(task.taskType, copy);
        }
        flushSubtasks();
      }
    }
  }

  public void verifyTasks(List<TaskInfo> subTasks) {
    addTasks(TaskType.UniverseUpdateSucceeded);
    if (hasRollingUpgrade) {
      addTasks(TaskType.ModifyBlackList);
    }
    Map<Integer, List<TaskInfo>> currentTasks =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    currentTasks.keySet().stream()
        .sorted()
        .forEach(
            position -> {
              log.debug(
                  "#{} Current -> {} x{}",
                  position,
                  CommissionerBaseTest.getBriefTaskInfo(currentTasks.get(position).get(0)),
                  currentTasks.get(position).size());
            });
    log.debug("============");
    subtasksByPosition.keySet().stream()
        .sorted()
        .forEach(
            position -> {
              log.debug(
                  "#{} Expected -> {} x{}",
                  position,
                  CommissionerBaseTest.getBriefTaskInfo(subtasksByPosition.get(position).get(0)),
                  subtasksByPosition.get(position).size());
            });

    currentTasks.keySet().stream()
        .sorted()
        .forEach(
            position -> {
              TaskInfo actualTaskInfo = currentTasks.get(position).get(0);
              int currentSize = currentTasks.get(position).size();

              List<TaskInfo> expected = subtasksByPosition.get(position);
              TaskInfo expectedTaskInfo = null;
              int expectedSize = 0;
              if (expected != null && !expected.isEmpty()) {
                expectedTaskInfo = expected.get(0);
                expectedSize = expected.size();
              }
              if (expectedTaskInfo == null) {
                fail(
                    "Expected "
                        + actualTaskInfo.getTaskType()
                        + " on position "
                        + position
                        + " but was null");
              } else {
                assertEquals(
                    "At position " + position,
                    expectedTaskInfo.getTaskType(),
                    actualTaskInfo.getTaskType());
                assertEquals("At position " + position, expectedSize, currentSize);
              }
            });

    assertEquals(subtasksByPosition.size(), currentTasks.size());
  }
}
