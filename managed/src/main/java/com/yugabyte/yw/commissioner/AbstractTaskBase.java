// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.alerts.AlertDefinitionLabelsBuilder;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.CustomerRegisterFormData;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.KnownAlertCodes;
import com.yugabyte.yw.models.helpers.KnownAlertTypes;
import com.yugabyte.yw.models.helpers.NodeDetails;
import lombok.extern.slf4j.Slf4j;
import play.Application;
import play.api.Play;
import play.libs.Json;

import javax.inject.Inject;
import java.util.UUID;
import java.util.concurrent.*;

@Slf4j
public abstract class AbstractTaskBase implements ITask {

  // Number of concurrent tasks to execute at a time.
  private static final int TASK_THREADS = 10;

  // The maximum time that excess idle threads will wait for new tasks before terminating.
  // The unit is specified in the API (and is seconds).
  private static final long THREAD_ALIVE_TIME = 60L;

  // The params for this task.
  protected ITaskParams taskParams;

  // The threadpool on which the tasks are executed.
  protected ExecutorService executor;

  // The sequence of task lists that should be executed.
  protected SubTaskGroupQueue subTaskGroupQueue;

  // The UUID of the top-level user-facing task at the top of Task tree. Eg. CreateUniverse, etc.
  protected UUID userTaskUUID;

  // A field used to send additional information with prometheus metric associated with this task
  public String taskInfo = "";

  protected final Application application;
  protected final play.Environment environment;
  protected final Config config;
  protected final ConfigHelper configHelper;
  protected final RuntimeConfigFactory runtimeConfigFactory;
  protected final AlertService alertService;
  protected final AlertDefinitionService alertDefinitionService;
  protected final YBClientService ybService;
  protected final TableManager tableManager;

  @Inject
  protected AbstractTaskBase(BaseTaskDependencies baseTaskDependencies) {
    this.application = baseTaskDependencies.getApplication();
    this.environment = baseTaskDependencies.getEnvironment();
    this.config = baseTaskDependencies.getConfig();
    this.configHelper = baseTaskDependencies.getConfigHelper();
    this.runtimeConfigFactory = baseTaskDependencies.getRuntimeConfigFactory();
    this.alertService = baseTaskDependencies.getAlertService();
    this.alertDefinitionService = baseTaskDependencies.getAlertDefinitionService();
    this.ybService = baseTaskDependencies.getYbService();
    this.tableManager = baseTaskDependencies.getTableManager();
  }

  protected ITaskParams taskParams() {
    return taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    this.taskParams = params;
  }

  @Override
  public String getName() {
    return this.getClass().getSimpleName();
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  @Override
  public String toString() {
    return getName() + " : details=" + getTaskDetails();
  }

  @Override
  public abstract void run();

  // Create an task pool which can handle an unbounded number of tasks, while using an initial set
  // of threads which get spawned upto TASK_THREADS limit.
  public void createThreadpool() {
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("TaskPool-" + getName() + "-%d").build();
    executor =
        new ThreadPoolExecutor(
            TASK_THREADS,
            TASK_THREADS,
            THREAD_ALIVE_TIME,
            TimeUnit.SECONDS,
            new LinkedBlockingQueue<Runnable>(),
            namedThreadFactory);
  }

  @Override
  public void setUserTaskUUID(UUID userTaskUUID) {
    this.userTaskUUID = userTaskUUID;
  }

  /** @param response : ShellResponse object */
  public void processShellResponse(ShellResponse response) {
    if (response.code != 0) {
      throw new RuntimeException((response.message != null) ? response.message : "error");
    }
  }

  /**
   * We would try to parse the shell response message as JSON and return JsonNode
   *
   * @param response: ShellResponse object
   * @return JsonNode: Json formatted shell response message
   */
  public JsonNode parseShellResponseAsJson(ShellResponse response) {
    return Util.convertStringToJson(response.message);
  }

  public UniverseUpdater nodeStateUpdater(
      final UUID universeUUID, final String nodeName, final NodeDetails.NodeState state) {
    UniverseUpdater updater =
        new UniverseUpdater() {
          public void run(Universe universe) {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            NodeDetails node = universe.getNode(nodeName);
            if (node == null) {
              return;
            }
            log.info(
                "Changing node {} state from {} to {} in universe {}.",
                nodeName,
                node.state,
                state,
                universeUUID);
            node.state = state;
            if (state == NodeDetails.NodeState.Decommissioned) {
              node.cloudInfo.private_ip = null;
              node.cloudInfo.public_ip = null;
            }

            // Update the node details.
            universeDetails.nodeDetailsSet.add(node);
            universe.setUniverseDetails(universeDetails);
          }
        };
    return updater;
  }

  @Override
  public boolean shouldSendNotification() {
    try {
      CustomerTask task = CustomerTask.findByTaskUUID(userTaskUUID);
      Customer customer = Customer.get(task.getCustomerUUID());
      CustomerConfig config = CustomerConfig.getAlertConfig(customer.uuid);
      CustomerRegisterFormData.AlertingData alertingData =
          Json.fromJson(config.data, CustomerRegisterFormData.AlertingData.class);
      return task.getType().equals(CustomerTask.TaskType.Create)
          && task.getTarget().equals(CustomerTask.TargetType.Backup)
          && alertingData.reportBackupFailures;
    } catch (Exception e) {
      return false;
    }
  }

  @Override
  public void sendNotification() {
    CustomerTask task = CustomerTask.findByTaskUUID(userTaskUUID);
    Customer customer = Customer.get(task.getCustomerUUID());
    String content =
        String.format(
            "%s %s failed for %s.\n\nTask Info: %s",
            task.getType().name(),
            task.getTarget().name(),
            task.getNotificationTargetName(),
            taskInfo);

    AlertDefinitionLabelsBuilder labelsBuilder = AlertDefinitionLabelsBuilder.create();
    if (task.getTarget().isUniverseTarget()) {
      Universe universe = Universe.maybeGet(task.getTargetUUID()).orElse(null);
      if (universe == null) {
        log.warn("Missing universe with UUID {}", task.getTargetUUID());
      } else {
        labelsBuilder.appendTarget(universe);
      }
    } else {
      labelsBuilder.appendTarget(customer);
    }
    Alert alert =
        new Alert()
            .setCustomerUUID(customer.getUuid())
            .setErrCode(KnownAlertCodes.TASK_FAILURE)
            .setType(KnownAlertTypes.Error)
            .setMessage(content)
            .setSendEmail(true)
            .setLabels(labelsBuilder.getAlertLabels());
    alertService.create(alert);
  }

  /**
   * Creates task with appropriate dependency injection
   *
   * @param taskClass task class
   * @return Task instance with injected dependencies
   */
  public static <T> T createTask(Class<T> taskClass) {
    return Play.current().injector().instanceOf(taskClass);
  }
}
