// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.google.api.client.util.Strings;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.logging.LogUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.Transactional;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.lang.reflect.Field;
import java.time.Duration;
import java.time.Instant;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.GeneratedValue;
import javax.persistence.GenerationType;
import javax.persistence.Id;
import lombok.Getter;
import lombok.Setter;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.MDC;
import play.data.validation.Constraints;

@Entity
@ApiModel(
    description = "Customer task information. A customer task has a _target_ and a _task type_.")
@Getter
@Setter
public class CustomerTask extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(CustomerTask.class);

  public enum TargetType {
    @EnumValue("Universe")
    Universe(true),

    @EnumValue("Cluster")
    Cluster(true),

    @EnumValue("Table")
    Table(true),

    @EnumValue("Provider")
    Provider(false),

    @EnumValue("Node")
    Node(true),

    @EnumValue("Backup")
    Backup(false),

    @EnumValue("Schedule")
    Schedule(false),

    @EnumValue("Customer Configuration")
    CustomerConfiguration(false),

    @EnumValue("KMS Configuration")
    KMSConfiguration(false),

    @EnumValue("XCluster Configuration")
    XClusterConfig(true),

    @EnumValue("Disaster Recovery Config")
    DrConfig(true),

    @EnumValue("Universe Key")
    UniverseKey(true),

    @EnumValue("Master Key")
    MasterKey(true),

    @EnumValue("Node Agent")
    NodeAgent(false);

    private final boolean universeTarget;

    TargetType(boolean universeTarget) {
      this.universeTarget = universeTarget;
    }

    public boolean isUniverseTarget() {
      return universeTarget;
    }
  }

  public enum TaskType {
    @EnumValue("Create")
    Create,

    @EnumValue("Pause")
    Pause,

    @EnumValue("Resume")
    Resume,

    @EnumValue("Update")
    Update,

    @EnumValue("Delete")
    Delete,

    @EnumValue("Stop")
    Stop,

    @EnumValue("Start")
    Start,

    @EnumValue("Restart")
    Restart,

    @EnumValue("Remove")
    Remove,

    @EnumValue("Add")
    Add,

    @EnumValue("Release")
    Release,

    @EnumValue("Reboot")
    Reboot,

    @EnumValue("Hard Reboot")
    HardReboot,

    @EnumValue("Edit")
    Edit,

    @EnumValue("Synchronize")
    Sync,

    @EnumValue("LdapSync")
    LdapSync,

    @EnumValue("RestartUniverse")
    RestartUniverse,

    @EnumValue("SoftwareUpgrade")
    SoftwareUpgrade,

    @EnumValue("SoftwareUpgradeYB")
    SoftwareUpgradeYB,

    @EnumValue("FinalizeUpgrade")
    FinalizeUpgrade,

    @EnumValue("RollbackUpgrade")
    RollbackUpgrade,

    @EnumValue("GFlagsUpgrade")
    GFlagsUpgrade,

    @EnumValue("KubernetesOverridesUpgrade")
    KubernetesOverridesUpgrade,

    @EnumValue("CertsRotate")
    CertsRotate,

    @EnumValue("TlsToggle")
    TlsToggle,

    @EnumValue("VMImageUpgrade")
    VMImageUpgrade,

    @EnumValue("SystemdUpgrade")
    SystemdUpgrade,

    @EnumValue("RebootUniverse")
    RebootUniverse,

    @Deprecated
    @EnumValue("UpgradeSoftware")
    UpgradeSoftware,

    @Deprecated
    @EnumValue("UpgradeVMImage")
    UpgradeVMImage,

    @EnumValue("ResizeNode")
    ResizeNode,

    @Deprecated
    @EnumValue("UpdateCert")
    UpdateCert,

    @EnumValue("UpdateDiskSize")
    UpdateDiskSize,

    @Deprecated
    @EnumValue("UpgradeGflags")
    UpgradeGflags,

    @EnumValue("UpdateLoadBalancerConfig")
    UpdateLoadBalancerConfig,

    @EnumValue("BulkImportData")
    BulkImportData,

    @Deprecated
    @EnumValue("Backup")
    Backup,

    @EnumValue("Restore")
    Restore,

    @EnumValue("CreatePitrConfig")
    CreatePitrConfig,

    @EnumValue("DeletePitrConfig")
    DeletePitrConfig,

    @EnumValue("RestoreSnapshotSchedule")
    RestoreSnapshotSchedule,

    @Deprecated
    @EnumValue("SetEncryptionKey")
    SetEncryptionKey,

    @EnumValue("EnableEncryptionAtRest")
    EnableEncryptionAtRest,

    @EnumValue("SetActiveUniverseKeys")
    SetActiveUniverseKeys,

    @EnumValue("RotateEncryptionKey")
    RotateEncryptionKey,

    @EnumValue("DisableEncryptionAtRest")
    DisableEncryptionAtRest,

    @EnumValue("StartMaster")
    StartMaster,

    @EnumValue("CreateAlertDefinitions")
    CreateAlertDefinitions,

    @EnumValue("ManageAlertDefinitions")
    ManageAlertDefinitions,

    @EnumValue("ExternalScript")
    ExternalScript,

    /**
     * @deprecated TargetType name must not be part of TaskType. Use {@link #Create} instead.
     */
    @Deprecated
    @EnumValue("CreateXClusterConfig")
    CreateXClusterConfig,

    /**
     * @deprecated TargetType name must not be part of TaskType. Use {@link #Edit} instead.
     */
    @Deprecated
    @EnumValue("EditXClusterConfig")
    EditXClusterConfig,

    /**
     * @deprecated TargetType name must not be part of TaskType. Use {@link #Delete} instead.
     */
    @Deprecated
    @EnumValue("DeleteXClusterConfig")
    DeleteXClusterConfig,

    /**
     * @deprecated TargetType name must not be part of TaskType. Use {@link #Sync} instead.
     */
    @Deprecated
    @EnumValue("SyncXClusterConfig")
    SyncXClusterConfig,

    @EnumValue("Failover")
    Failover,

    @EnumValue("Switchover")
    Switchover,

    @EnumValue("PrecheckNode")
    PrecheckNode,

    @EnumValue("Abort")
    Abort,

    @EnumValue("CreateSupportBundle")
    CreateSupportBundle,

    @EnumValue("CreateTableSpaces")
    CreateTableSpaces,

    @EnumValue("ThirdpartySoftwareUpgrade")
    ThirdpartySoftwareUpgrade,

    @EnumValue("ModifyAuditLoggingConfig")
    ModifyAuditLoggingConfig,

    @EnumValue("RotateAccessKey")
    RotateAccessKey,

    @EnumValue("CreateAndRotateAccessKey")
    CreateAndRotateAccessKey,

    @EnumValue("RunApiTriggeredHooks")
    RunApiTriggeredHooks,

    @EnumValue("InstallYbcSoftware")
    InstallYbcSoftware,

    @EnumValue("InstallYbcSoftwareOnK8s")
    InstallYbcSoftwareOnK8s,

    @EnumValue("UpgradeUniverseYbc")
    UpgradeUniverseYbc,

    @EnumValue("DisableYbc")
    DisableYbc,

    @EnumValue("ConfigureDBApis")
    ConfigureDBApis,

    @EnumValue("ConfigureDBApisKubernetes")
    ConfigureDBApisKubernetes,

    @EnumValue("CreateImageBundle")
    CreateImageBundle,

    @EnumValue("ReprovisionNode")
    ReprovisionNode,

    @EnumValue("Install")
    Install;

    public String toString(boolean completed) {
      switch (this) {
        case Add:
          return completed ? "Added " : "Adding ";
        case Create:
          return completed ? "Created " : "Creating ";
        case Pause:
          return completed ? "Paused " : "Pausing ";
        case Release:
          return completed ? "Released " : "Releasing ";
        case Reboot:
        case HardReboot:
          return completed ? "Rebooted " : "Rebooting ";
        case Remove:
          return completed ? "Removed " : "Removing ";
        case ResizeNode:
          return completed ? "Resized Node " : "Resizing Node ";
        case Resume:
          return completed ? "Resumed " : "Resuming ";
        case Start:
          return completed ? "Started " : "Starting ";
        case Stop:
          return completed ? "Stopped " : "Stopping ";
        case Update:
          return completed ? "Updated " : "Updating ";
        case Delete:
          return completed ? "Deleted " : "Deleting ";
        case Edit:
          return completed ? "Edited " : "Editing ";
        case Sync:
          return completed ? "Synchronized " : "Synchronizing ";
        case LdapSync:
          return completed ? "LDAP Sync Completed on " : "LDAP Sync in Progress on ";
        case RestartUniverse:
          return completed ? "Restarted " : "Restarting ";
        case SoftwareUpgrade:
          return completed ? "Upgraded Software " : "Upgrading Software ";
        case SoftwareUpgradeYB:
          return completed ? "Upgraded Software " : "Upgrading Software ";
        case FinalizeUpgrade:
          return completed ? "Finalized Upgrade" : "Finalizing Upgrade";
        case RollbackUpgrade:
          return completed ? "Rolled back upgrade" : "Rolling backup upgrade";
        case SystemdUpgrade:
          return completed ? "Upgraded to Systemd " : "Upgrading to Systemd ";
        case GFlagsUpgrade:
          return completed ? "Upgraded GFlags " : "Upgrading GFlags ";
        case KubernetesOverridesUpgrade:
          return completed ? "Upgraded Kubernetes Overrides " : "Upgrading Kubernetes Overrides ";
        case CertsRotate:
          return completed ? "Updated Certificates " : "Updating Certificates ";
        case TlsToggle:
          return completed ? "Toggled TLS " : "Toggling TLS ";
        case VMImageUpgrade:
        case UpgradeVMImage:
          return completed ? "Upgraded VM Image " : "Upgrading VM Image ";
        case UpgradeSoftware:
          return completed ? "Upgraded Software " : "Upgrading Software ";
        case UpdateDiskSize:
          return completed ? "Updated Disk Size " : "Updating Disk Size ";
        case UpdateLoadBalancerConfig:
          return completed ? "Updated Load Balancer Config " : "Updating Load Balancer Config ";
        case UpdateCert:
          return completed ? "Updated Cert " : "Updating Cert ";
        case UpgradeGflags:
          return completed ? "Upgraded GFlags " : "Upgrading GFlags ";
        case BulkImportData:
          return completed ? "Bulk imported data" : "Bulk importing data";
        case Restore:
          return completed ? "Restored " : "Restoring ";
        case CreatePitrConfig:
          return completed ? "Created PITR Config" : "Creating PITR Config";
        case DeletePitrConfig:
          return completed ? "Deleted PITR Config" : "Deleting PITR Config";
        case RestoreSnapshotSchedule:
          return completed ? "Restored Snapshot Schedule" : "Restoring Snapshot Schedule";
        case Restart:
          return completed ? "Restarted " : "Restarting ";
        case Backup:
          return completed ? "Backed up " : "Backing up ";
        case SetEncryptionKey:
          return completed ? "Set encryption key" : "Setting encryption key";
        case EnableEncryptionAtRest:
          return completed ? "Enabled encryption at rest" : "Enabling encryption at rest";
        case SetActiveUniverseKeys:
          return completed ? "Set active universe keys" : "Setting active universe keys";
        case RotateEncryptionKey:
          return completed ? "Rotated encryption at rest" : "Rotating encryption at rest";
        case DisableEncryptionAtRest:
          return completed ? "Disabled encryption at rest" : "Disabling encryption at rest";
        case StartMaster:
          return completed ? "Started Master process on " : "Starting Master process on ";
        case CreateAlertDefinitions:
          return completed ? "Created alert definitions " : "Creating alert definitions ";
        case ManageAlertDefinitions:
          return completed ? "Managed alert definitions " : "Managing alert definitions ";
        case ExternalScript:
          return completed ? "Script execution completed " : "Script execution is running";
        case CreateXClusterConfig:
          return completed ? "Created xcluster config " : "Creating xcluster config ";
        case DeleteXClusterConfig:
          return completed ? "Deleted xcluster config " : "Deleting xcluster config ";
        case EditXClusterConfig:
          return completed ? "Edited xcluster config " : "Editing xcluster config ";
        case SyncXClusterConfig:
          return completed ? "Synchronized xcluster config " : "Synchronizing xcluster config ";
        case Failover:
          return completed ? "Failed over dr config " : "Failing over dr config ";
        case Switchover:
          return completed ? "Switched over dr config " : "Switching over dr config ";
        case PrecheckNode:
          return completed ? "Performed preflight check on " : "Performing preflight check on ";
        case Abort:
          return completed ? "Task aborted " : "Aborting task ";
        case CreateSupportBundle:
          return completed ? "Created Support Bundle in " : "Creating Support Bundle in ";
        case ThirdpartySoftwareUpgrade:
          return completed
              ? "Upgraded third-party software for "
              : "Upgrading third-party software for ";
        case ModifyAuditLoggingConfig:
          return completed
              ? "Modified audit logging config for "
              : "Modifying audit logging config for ";
        case CreateTableSpaces:
          return completed ? "Created tablespaces in " : "Creating tablespaces in ";
        case RotateAccessKey:
          return completed ? "Rotated Access Key" : "Rotating Access Key";
        case RebootUniverse:
          return completed ? "Rebooted " : "Rebooting ";
        case CreateAndRotateAccessKey:
          return completed
              ? "Creating Access Key and Rotation Tasks"
              : "Created New Access Key and Rotation Tasks";
        case RunApiTriggeredHooks:
          return completed ? "Ran API Triggered Hooks" : "Running API Triggered Hooks";
        case InstallYbcSoftware:
          return completed ? "Installed Ybc" : "Installing Ybc";
        case InstallYbcSoftwareOnK8s:
          return completed ? "Installed Ybc on K8S" : "Installing Ybc on K8S";
        case UpgradeUniverseYbc:
          return completed ? "Upgraded Ybc" : "Upgrading Ybc";
        case DisableYbc:
          return completed ? "Disabled Ybc" : "Disabling Ybc";
        case ConfigureDBApisKubernetes:
        case ConfigureDBApis:
          return completed ? "Configured DB APIs" : "Configuring DB APIs";
        case CreateImageBundle:
          return completed ? "Created" : "Creating";
        case ReprovisionNode:
          return completed ? "Reprovisioned" : "Reprovisioning";
        case Install:
          return completed ? "Installed" : "Installing";
        default:
          return null;
      }
    }

    public static List<TaskType> filteredValues() {
      return Arrays.stream(TaskType.values())
          .filter(
              value -> {
                try {
                  Field field = TaskType.class.getField(value.name());
                  return !field.isAnnotationPresent(Deprecated.class);
                } catch (Exception e) {
                  return false;
                }
              })
          .collect(Collectors.toList());
    }

    public String getFriendlyName() {
      switch (this) {
        case StartMaster:
          return "Start Master Process on";
        case PrecheckNode:
          return "Precheck";
        case RebootUniverse:
          return "Reboot";
        case RestartUniverse:
          return "Restart";
        case ReprovisionNode:
          return "Re-provision";
        default:
          return toFriendlyTypeName();
      }
    }

    private String toFriendlyTypeName() {
      return name().replaceAll("([a-z]+)([A-Z]+)", "$1 $2");
    }
  }

  // Use IDENTITY strategy because `customer_task.id` is a `bigserial` type; not a sequence.
  @Id
  @GeneratedValue(strategy = GenerationType.IDENTITY)
  @ApiModelProperty(value = "Customer task UUID", accessMode = READ_ONLY)
  private Long id;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY, required = true)
  private UUID customerUUID;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "Task UUID", accessMode = READ_ONLY, required = true)
  private UUID taskUUID;

  public CustomerTask updateTaskUUID(UUID newTaskUUID) {
    this.taskUUID = newTaskUUID;
    save();
    return this;
  }

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "Task type", accessMode = READ_ONLY, required = true)
  private TaskType type;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "Task target type", accessMode = READ_ONLY, required = true)
  private TargetType targetType;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "Task target name", accessMode = READ_ONLY, required = true)
  private String targetName;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "Task target UUID", accessMode = READ_ONLY, required = true)
  private UUID targetUUID;

  @Constraints.Required
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Creation time",
      accessMode = READ_ONLY,
      example = "2021-06-17T15:00:05-0400",
      required = true)
  private Date createTime;

  @Column
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "Completion time (present only if a task has completed)",
      accessMode = READ_ONLY,
      example = "2021-06-17T15:00:05-0400")
  private Date completionTime;

  public void resetCompletionTime() {
    this.completionTime = null;
    this.save();
  }

  @Column
  @ApiModelProperty(value = "Custom type name", accessMode = READ_ONLY, example = "TLS Toggle ON")
  private String customTypeName;

  @Column
  @ApiModelProperty(
      value = "Correlation id",
      accessMode = READ_ONLY,
      example = "3e6ac43a-15d9-46c0-831c-460775ce87ad")
  private String correlationId;

  @Column
  @ApiModelProperty(
      value = "User triggering task",
      accessMode = READ_ONLY,
      example = "shagarwal@yugabyte.com")
  private String userEmail;

  public void markAsCompleted() {
    markAsCompleted(new Date());
  }

  @VisibleForTesting
  void markAsCompleted(Date completionTime) {
    if (this.completionTime == null) {
      this.completionTime = completionTime;
      this.save();
    }
  }

  private static final Set<TaskType> upgradeCustomerTasksSet =
      ImmutableSet.of(
          CustomerTask.TaskType.FinalizeUpgrade,
          CustomerTask.TaskType.RollbackUpgrade,
          CustomerTask.TaskType.SoftwareUpgrade);

  public static final Finder<Long, CustomerTask> find =
      new Finder<Long, CustomerTask>(CustomerTask.class) {};

  public static CustomerTask create(
      Customer customer,
      UUID targetUUID,
      UUID taskUUID,
      TargetType targetType,
      TaskType type,
      String targetName,
      @Nullable String customTypeName,
      @Nullable String userEmail) {
    CustomerTask th = new CustomerTask();
    th.customerUUID = customer.getUuid();
    th.targetUUID = targetUUID;
    th.taskUUID = taskUUID;
    th.targetType = targetType;
    th.type = type;
    th.targetName = targetName;
    th.createTime = new Date();
    th.customTypeName = customTypeName;
    String emailFromContext = Util.maybeGetEmailFromContext();
    if (emailFromContext.equals("Unknown")) {
      // When task is not created as a part of user action get email of the scheduler.
      String emailFromSchedule = maybeGetEmailFromSchedule();
      if (emailFromSchedule.equals("Unknown")) {
        if (!StringUtils.isEmpty(userEmail)) {
          th.userEmail = userEmail;
        } else {
          th.userEmail = "Unknown";
        }
      } else {
        th.userEmail = emailFromSchedule;
      }
    } else {
      th.userEmail = emailFromContext;
    }
    String correlationId = (String) MDC.get(LogUtil.CORRELATION_ID);
    if (!Strings.isNullOrEmpty(correlationId)) th.correlationId = correlationId;
    th.save();
    return th;
  }

  public static CustomerTask create(
      Customer customer,
      UUID targetUUID,
      UUID taskUUID,
      TargetType targetType,
      TaskType type,
      String targetName) {
    return create(customer, targetUUID, taskUUID, targetType, type, targetName, null);
  }

  public static CustomerTask create(
      Customer customer,
      UUID targetUUID,
      UUID taskUUID,
      TargetType targetType,
      TaskType type,
      String targetName,
      @Nullable String customTypeName) {
    return create(
        customer, targetUUID, taskUUID, targetType, type, targetName, customTypeName, null);
  }

  public static CustomerTask get(Long id) {
    return CustomerTask.find.query().where().idEq(id).findOne();
  }

  public static List<CustomerTask> getByCustomerUUID(UUID customerUUID) {
    return CustomerTask.find.query().where().eq("customer_uuid", customerUUID).findList();
  }

  @Deprecated
  public static CustomerTask get(UUID customerUUID, UUID taskUUID) {
    return CustomerTask.find
        .query()
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("task_uuid", taskUUID)
        .findOne();
  }

  public static CustomerTask getOrBadRequest(UUID customerUUID, UUID taskUUID) {
    CustomerTask customerTask = get(customerUUID, taskUUID);
    if (customerTask == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Customer Task UUID: " + taskUUID);
    }
    return customerTask;
  }

  public String getFriendlyDescription() {
    StringBuilder sb = new StringBuilder();
    sb.append(type.toString(completionTime != null));
    sb.append(targetType.name());
    sb.append(" : ").append(targetName);
    return sb.toString();
  }

  /**
   * Deletes customer_task, task_info and all its subtasks of a given task. Assumes task_info tree
   * is one level deep. If this assumption changes then this code needs to be reworked to recurse.
   * When successful; it deletes at least 2 rows because there is always customer_task and
   * associated task_info row that get deleted.
   *
   * @return number of rows deleted. ==0 - if deletion was skipped due to data integrity issues. >=2
   *     - number of rows deleted
   */
  @Transactional
  public int cascadeDeleteCompleted() {
    Preconditions.checkNotNull(
        completionTime, String.format("CustomerTask %s has not completed", id));
    Optional<TaskInfo> optional = TaskInfo.maybeGet(taskUUID);
    if (!optional.isPresent()) {
      delete();
      return 1;
    }
    TaskInfo rootTaskInfo = optional.get();
    if (!rootTaskInfo.hasCompleted()) {
      LOG.warn(
          "Completed CustomerTask(id:{}, type:{}) has incomplete task_info {}",
          id,
          type,
          rootTaskInfo);
      return 0;
    }
    int subTaskSize = rootTaskInfo.getSubTasks().size();
    deleteTasks(rootTaskInfo);
    delete();
    return 2 + subTaskSize;
  }

  @Transactional
  // This is in a transaction block to not lose the parent task UUID.
  private void deleteTasks(TaskInfo rootTaskInfo) {
    rootTaskInfo.delete();
    // TODO This is needed temporarily if the migration to add the FK constraint is skipped.
    rootTaskInfo.getSubTasks().stream().forEach(TaskInfo::delete);
  }

  public static CustomerTask findByTaskUUID(UUID taskUUID) {
    return find.query().where().eq("task_uuid", taskUUID).findOne();
  }

  public static List<CustomerTask> findOlderThan(Customer customer, Duration duration) {
    Date cutoffDate = new Date(Instant.now().minus(duration).toEpochMilli());
    return find.query()
        .where()
        .eq("customerUUID", customer.getUuid())
        .le("completion_time", cutoffDate)
        .findList();
  }

  public static List<CustomerTask> findIncompleteByTargetUUID(UUID targetUUID) {
    return find.query().where().eq("target_uuid", targetUUID).isNull("completion_time").findList();
  }

  public static CustomerTask getLastTaskByTargetUuid(UUID targetUUID) {
    List<CustomerTask> tasks =
        find.query()
            .where()
            .eq("target_uuid", targetUUID)
            .isNotNull("completion_time")
            .orderBy("completion_time desc")
            .setMaxRows(1)
            .findList();
    if (tasks.size() > 0) {
      return tasks.get(0);
    } else {
      return null;
    }
  }

  public String getNotificationTargetName() {
    if (getType().equals(TaskType.Create) && getTargetType().equals(TargetType.Backup)) {
      return Universe.getOrBadRequest(getTargetUUID()).getName();
    } else {
      return getTargetName();
    }
  }

  private static String maybeGetEmailFromSchedule() {
    return Schedule.getAllActive().stream()
        .filter(Schedule::isRunningState)
        .findAny()
        .map(Schedule::getUserEmail)
        .orElse("Unknown");
  }

  public boolean isDeletable() {
    if (targetType.isUniverseTarget()) {
      Optional<Universe> optional = Universe.maybeGet(targetUUID);
      if (!optional.isPresent()) {
        return true;
      }
      if (upgradeCustomerTasksSet.contains(type)) {
        LOG.debug("Universe task {} is not deletable as it is an upgrade task.", targetUUID);
        return false;
      }
      UniverseDefinitionTaskParams taskParams = optional.get().getUniverseDetails();
      if (taskUUID.equals(taskParams.updatingTaskUUID)) {
        LOG.debug("Universe task {} is not deletable", targetUUID);
        return false;
      }
      if (taskUUID.equals(taskParams.placementModificationTaskUuid)) {
        LOG.debug("Universe task {} is not deletable", targetUUID);
        return false;
      }
    } else if (targetType == TargetType.Provider) {
      Optional<Provider> optional = Provider.maybeGet(targetUUID);
      if (!optional.isPresent()) {
        return true;
      }
      CustomerTask lastTask = CustomerTask.getLastTaskByTargetUuid(targetUUID);
      if (lastTask == null) {
        // Not possible.
        return true;
      }
      if (taskUUID.equals(lastTask.taskUUID)) {
        LOG.debug("Provider task {} is not deletable", targetUUID);
        return false;
      }
    }
    return true;
  }
}
