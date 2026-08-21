// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.tasks;

import static com.yugabyte.yw.models.CustomerTask.createQueryByFilter;
import static com.yugabyte.yw.models.helpers.CommonUtils.performPagedQuery;

import api.v2.handlers.HandlerPagingSupport;
import api.v2.mappers.TaskMapper;
import api.v2.models.TaskPagedQuerySpec;
import api.v2.models.TaskPagedResp;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.base.Strings;
import com.google.common.collect.Lists;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.CustomerTaskFormData;
import com.yugabyte.yw.forms.filters.TaskApiFilter;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.filters.TaskFilter;
import com.yugabyte.yw.models.paging.PagedQuery;
import com.yugabyte.yw.models.paging.PagedQuery.SortByIF;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import com.yugabyte.yw.models.paging.TaskPagedApiResponse;
import com.yugabyte.yw.models.paging.TaskPagedQuery;
import com.yugabyte.yw.models.paging.TaskPagedResponse;
import io.ebean.Query;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
@Singleton
public class CustomerTaskHandler {

  private final RuntimeConfGetter confGetter;
  private final Commissioner commissioner;

  @Inject
  public CustomerTaskHandler(RuntimeConfGetter confGetter, Commissioner commissioner) {
    this.confGetter = confGetter;
    this.commissioner = commissioner;
  }

  @Getter
  public enum SortBy implements PagedQuery.SortByIF {
    createTime("createTime");

    private final String sortField;

    SortBy(String sortField) {
      this.sortField = sortField;
    }

    @Override
    public SortByIF getOrderField() {
      return SortBy.createTime;
    }
  }

  public TaskPagedApiResponse pagedList(TaskPagedQuery pagedQuery, Customer customer) {
    if (pagedQuery.getSortBy() == null) {
      pagedQuery.setSortBy(SortBy.createTime);
      pagedQuery.setDirection(SortDirection.DESC);
    }
    Query<CustomerTask> query = createQueryByFilter(pagedQuery.getFilter()).query();
    TaskPagedResponse response = performPagedQuery(query, pagedQuery, TaskPagedResponse.class);
    return createResponse(response, customer);
  }

  public TaskPagedResp pageListTasks(UUID cUUID, TaskPagedQuerySpec spec) {
    Customer customer = Customer.getOrNotFound(cUUID);
    TaskPagedApiResponse response = pagedList(toPagedQuery(cUUID, spec), customer);
    return HandlerPagingSupport.pagedResponse(
        new TaskPagedResp(), response, TaskMapper.INSTANCE::toTask);
  }

  public Map<UUID, List<CustomerTaskFormData>> buildTaskListMap(
      Customer customer, List<CustomerTask> customerTaskList) {
    Map<UUID, List<CustomerTaskFormData>> taskListMap = new HashMap<>();
    Map<UUID, CustomerTask> lastTaskByTargetMap = buildLastTaskByTargetMap(customerTaskList);
    Map<UUID, Set<String>> allowRetryTasksByTargetMap =
        buildAllowRetryTasksByTargetMap(customer, null /* specific target UUID */);
    List<List<CustomerTask>> batches =
        Lists.partition(
            customerTaskList,
            confGetter.getConfForScope(customer, CustomerConfKeys.taskInfoDbQueryBatchSize));
    for (List<CustomerTask> batch : batches) {
      Map<UUID, List<TaskInfo>> subTaskInfos =
          TaskInfo.getSubTasks(
              batch.stream().map(CustomerTask::getTaskUUID).collect(Collectors.toSet()));
      for (CustomerTask task : batch) {
        commissioner
            .buildTaskStatus(
                task,
                subTaskInfos.getOrDefault(task.getTaskUUID(), Collections.emptyList()),
                allowRetryTasksByTargetMap,
                lastTaskByTargetMap)
            .ifPresent(
                taskProgress -> {
                  CustomerTaskFormData taskData = buildCustomerTaskFromData(task, taskProgress);
                  if (taskData != null) {
                    List<CustomerTaskFormData> taskList =
                        taskListMap.computeIfAbsent(task.getTargetUUID(), k -> new ArrayList<>());
                    taskList.add(taskData);
                  }
                });
      }
    }
    return taskListMap;
  }

  public Map<UUID, List<CustomerTaskFormData>> buildSingleTaskListMap(
      Customer customer, CustomerTask customerTask) {
    Map<UUID, List<CustomerTaskFormData>> taskListMap = new HashMap<>();
    Map<UUID, CustomerTask> lastTaskByTargetMap = new HashMap<>();
    CustomerTask lastCustomerTask =
        CustomerTask.getLastTaskByTargetUuid(customerTask.getTargetUUID());
    if (lastCustomerTask != null) {
      lastTaskByTargetMap.put(lastCustomerTask.getTargetUUID(), lastCustomerTask);
    }
    Map<UUID, Set<String>> allowRetryTasksByTargetMap =
        buildAllowRetryTasksByTargetMap(customer, customerTask.getTargetUUID());
    List<TaskInfo> subTaskInfos = customerTask.getTaskInfo().getSubTasks();
    commissioner
        .buildTaskStatus(
            customerTask, subTaskInfos, allowRetryTasksByTargetMap, lastTaskByTargetMap)
        .ifPresent(
            taskProgress -> {
              CustomerTaskFormData taskData = buildCustomerTaskFromData(customerTask, taskProgress);
              if (taskData != null) {
                taskData.subtaskInfos = subTaskInfos;
                taskListMap
                    .computeIfAbsent(customerTask.getTargetUUID(), k -> new ArrayList<>())
                    .add(taskData);
              }
            });
    return taskListMap;
  }

  private TaskPagedApiResponse createResponse(TaskPagedResponse response, Customer customer) {
    List<CustomerTask> tasks = response.getEntities();
    Map<UUID, List<TaskInfo>> subTaskInfos =
        TaskInfo.getSubTasks(
            tasks.stream().map(CustomerTask::getTaskUUID).collect(Collectors.toSet()));
    Map<UUID, CustomerTask> lastTaskByTargetMap = buildLastTaskByTargetMap(tasks);
    Map<UUID, Set<String>> allowRetryTasksByTargetMap =
        buildAllowRetryTasksByTargetMap(customer, null /* specific target */);
    List<CustomerTaskFormData> taskList =
        tasks.parallelStream()
            .map(
                r ->
                    commissioner
                        .buildTaskStatus(
                            r,
                            subTaskInfos.getOrDefault(r.getTaskUUID(), Collections.emptyList()),
                            allowRetryTasksByTargetMap,
                            lastTaskByTargetMap)
                        .map(taskProgress -> buildCustomerTaskFromData(r, taskProgress))
                        .orElse(null))
            .filter(Objects::nonNull)
            .collect(Collectors.toList());
    return response.setData(taskList, new TaskPagedApiResponse());
  }

  private static void maybeSetFromTaskInfo(ObjectNode node, TaskInfo taskInfo, String attr) {
    if (taskInfo.getTaskParams().has(attr)) {
      node.set(attr, taskInfo.getTaskParams().get(attr));
    }
  }

  private CustomerTaskFormData buildCustomerTaskFromData(
      CustomerTask task, ObjectNode taskProgress) {
    try {
      TaskInfo taskInfo = task.getTaskInfo();
      CustomerTaskFormData taskData = new CustomerTaskFormData();
      taskData.percentComplete = taskProgress.get("percent").asInt();
      taskData.status = taskProgress.get("status").asText();
      taskData.abortable = taskProgress.get("abortable").asBoolean();
      taskData.retryable = taskProgress.get("retryable").asBoolean();
      taskData.canRollback = taskProgress.get("canRollback").asBoolean();
      taskData.id = task.getTaskUUID();
      taskData.title = task.getFriendlyDescription();
      taskData.createTime = task.getCreateTime();
      taskData.completionTime = task.getCompletionTime();
      taskData.target = task.getTargetType().name();
      taskData.type = task.getType().name();
      taskData.typeName =
          task.getCustomTypeName() != null
              ? task.getCustomTypeName()
              : task.getType().getFriendlyName();
      taskData.targetUUID = task.getTargetUUID();
      taskData.userEmail = getTaskUserEmail(task, taskInfo);

      if (taskProgress.has("details")) {
        taskData.details = taskProgress.get("details");
      } else {
        ObjectNode details = Json.newObject();
        for (String attr : Set.of("auditLogConfig", "queryLogConfig", "metricsExportConfig")) {
          maybeSetFromTaskInfo(details, taskInfo, attr);
        }

        ObjectNode versionNumbers = commissioner.getVersionInfo(task, taskInfo);
        if (versionNumbers != null && !versionNumbers.isEmpty()) {
          details.set("versionNumbers", versionNumbers);
        }

        taskData.details = details;
      }

      String correlationId = task.getCorrelationId();
      if (!Strings.isNullOrEmpty(correlationId)) {
        taskData.correlationId = correlationId;
      }

      return taskData;
    } catch (RuntimeException e) {
      log.error("Error fetching task progress for {}", task.getTaskUUID(), e);
      return null;
    }
  }

  private String getTaskUserEmail(CustomerTask task, TaskInfo taskInfo) {
    String userEmail = task.getUserEmail();

    if ((Strings.isNullOrEmpty(userEmail) || "Unknown".equals(userEmail))
        && isKubernetesOperatorTask(taskInfo)) {
      return CustomerTask.BACKGROUND_TASK_USER;
    }

    return userEmail;
  }

  private boolean isKubernetesOperatorTask(TaskInfo taskInfo) {
    return taskInfo.getTaskParams() != null
        && taskInfo.getTaskParams().hasNonNull("kubernetesResourceDetails");
  }

  private Map<UUID, CustomerTask> buildLastTaskByTargetMap(List<CustomerTask> customerTaskList) {
    return customerTaskList.stream()
        .filter(c -> c.getCompletionTime() != null)
        .collect(
            Collectors.toMap(
                CustomerTask::getTargetUUID,
                Function.identity(),
                (c1, c2) -> c1.getCompletionTime().after(c2.getCompletionTime()) ? c1 : c2));
  }

  private Map<UUID, Set<String>> buildAllowRetryTasksByTargetMap(
      Customer customer, @Nullable UUID targetUuid) {
    Map<UUID, Set<String>> allowRetryTasksByTargetMap = new HashMap<>();
    Map<UUID, String> updatingTaskByTargetMap =
        commissioner.getUpdatingTaskUUIDsForTargets(customer.getId(), targetUuid);
    Map<UUID, String> placementModificationTaskByTargetMap =
        commissioner.getPlacementModificationTaskUUIDsForTargets(customer.getId(), targetUuid);

    updatingTaskByTargetMap.forEach(
        (universeUUID, taskUUID) ->
            allowRetryTasksByTargetMap
                .computeIfAbsent(universeUUID, k -> new HashSet<>())
                .add(taskUUID));
    placementModificationTaskByTargetMap.forEach(
        (universeUUID, taskUUID) ->
            allowRetryTasksByTargetMap
                .computeIfAbsent(universeUUID, k -> new HashSet<>())
                .add(taskUUID));

    return allowRetryTasksByTargetMap;
  }

  private TaskPagedQuery toPagedQuery(UUID cUUID, TaskPagedQuerySpec spec) {
    return HandlerPagingSupport.toPagedQuery(
        spec, new TaskPagedQuery(), SortBy.createTime, toTaskFilter(cUUID, spec.getFilter()));
  }

  private TaskFilter toTaskFilter(UUID cUUID, TaskApiFilter apiFilter) {
    if (apiFilter == null) {
      return TaskFilter.builder().customerUUID(cUUID).build();
    }

    return apiFilter.toFilter().toBuilder().customerUUID(cUUID).build();
  }
}
