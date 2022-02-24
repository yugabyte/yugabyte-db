// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.CreatedTimestamp;
import io.ebean.annotation.DbJson;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.function.Consumer;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.GeneratedValue;
import javax.persistence.GenerationType;
import javax.persistence.Id;
import javax.persistence.SequenceGenerator;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

@ApiModel(description = "Audit logging for requests and responses")
@Entity
public class Audit extends Model {

  public static final Logger LOG = LoggerFactory.getLogger(Audit.class);

  public enum TargetType {
    @EnumValue("Session")
    Session,

    @EnumValue("Cloud Provider")
    CloudProvider,

    @EnumValue("Region")
    Region,

    @EnumValue("Availability Zone")
    AvailabilityZone,

    @EnumValue("Instance Type")
    InstanceType,

    @EnumValue("Customer Configuration")
    CustomerConfig,

    @EnumValue("Encryption At Rest")
    EncryptionAtRest,

    @EnumValue("Customer")
    Customer,

    @EnumValue("Release")
    Release,

    @EnumValue("Certificate")
    Certificate,

    @EnumValue("Alert")
    Alert,

    @EnumValue("Maintenance Window")
    MaintenanceWindow,

    @EnumValue("Access Key")
    AccessKey,

    @EnumValue("Universe")
    Universe,

    @EnumValue("XCluster Configuration")
    XClusterConfig,

    @EnumValue("Table")
    Table,

    @EnumValue("Backup")
    Backup,

    @EnumValue("Customer Task")
    CustomerTask,

    @EnumValue("Node Instance")
    NodeInstance,

    @EnumValue("Schedule")
    Schedule,

    @EnumValue("User")
    User
  }

  public enum ActionType {
    @EnumValue("Register Session")
    RegisterSession,

    @EnumValue("Create Cloud Provider")
    CreateCloudProvider,

    @EnumValue("Delete Cloud Provider")
    DeleteCloudProvider,

    @EnumValue("Edit Cloud Provider")
    EditCloudProvider,

    @EnumValue("Refresh Cloud Provider Pricing")
    RefreshPricingCloudProvider,

    @EnumValue("Create Kubernetes Cloud Provider")
    CreateKubernetesCloudProvider,

    @EnumValue("Bootstrap Cloud Provider")
    BootstrapCloudProvider,

    @EnumValue("Setup Docker Cloud Provider")
    SetupDockerCloudProvider,

    @EnumValue("Create Region")
    CreateRegion,

    @EnumValue("Delete Region")
    DeleteRegion,

    @EnumValue("Create Availability Zone")
    CreateAvailabilityZone,

    @EnumValue("Delete Availability Zone")
    DeleteAvailabilityZone,

    @EnumValue("Create Instance")
    CreateInstance,

    @EnumValue("Delete Instance")
    DeleteInstance,

    @EnumValue("Create Customer Configuration")
    CreateCustomerConfiguration,

    @EnumValue("Edit Customer Configuration")
    EditCustomerConfiguration,

    @EnumValue("Delete Customer Configuration")
    DeleteCustomerConfiguration,

    @EnumValue("Create KMS Configuration")
    CreateKmsConfiguration,

    @EnumValue("Edit KMS Configuration")
    EditKmsConfiguration,

    @EnumValue("Delete KMS Configuration")
    DeleteKmsConfiguration,

    @EnumValue("Retrieve KMS Key")
    RetrieveKmsKey,

    @EnumValue("Remove KMS Key Reference History")
    RemoveKmsKeyReferenceHistory,

    @EnumValue("Delete Customer")
    DeleteCustomer,

    @EnumValue("Upsert Features Customer")
    UpsertFeaturesCustomer,

    @EnumValue("Create Release")
    CreateRelease,

    @EnumValue("Update Release")
    UpdateRelease,

    @EnumValue("Upload Certificate")
    UploadCertificate,

    @EnumValue("Delete Certificate")
    DeleteCertificate,

    @EnumValue("Get Client's Root Certificate")
    GetRootCertificate,

    @EnumValue("Add Client Certificate")
    AddClientCertificate,

    @EnumValue("Create Alert Configuration")
    CreateAlertConfiguration,

    @EnumValue("Edit Alert Configuration")
    EditAlertConfiguration,

    @EnumValue("Delete Alert Configuration")
    DeleteAlertConfiguration,

    @EnumValue("Create Alert Channel")
    CreateAlertChannel,

    @EnumValue("Update Alert Channel")
    UpdateAlertChannel,

    @EnumValue("Delete Alert Channel")
    DeleteAlertChannel,

    @EnumValue("Get Alert Destination")
    GetAlertDestination,

    @EnumValue("Update Alert Destination")
    UpdateAlertDestination,

    @EnumValue("Delete Alert Destination")
    DeleteAlertDestination,

    @EnumValue("Create Maintenance Window")
    CreateMaintenanceWindow,

    @EnumValue("Update Maintenance Window")
    UpdateMaintenanceWindow,

    @EnumValue("Delete Maintenance Window")
    DeleteMaintenanceWindow,

    @EnumValue("Create Access Key")
    CreateAccessKey,

    @EnumValue("Delete Access Key")
    DeleteAccessKey,

    @EnumValue("Create Universe")
    CreateUniverse,

    @EnumValue("Update Universe")
    UpdateUniverse,

    @EnumValue("Upgrade Universe")
    UpgradeUniverse,

    @EnumValue("Destroy Universe")
    DestroyUniverse,

    @EnumValue("Import Universe")
    ImportUniverse,

    @EnumValue("Pause Universe")
    PauseUniverse,

    @EnumValue("Resume Universe")
    ResumeUniverse,

    @EnumValue("Restart Universe")
    RestartUniverse,

    @EnumValue("Upgrade Software")
    UpgradeSoftware,

    @EnumValue("Upgrade GFlags")
    UpgradeGFlags,

    @EnumValue("Upgrade Certs")
    UpgradeCerts,

    @EnumValue("Upgrade TLS")
    UpgradeTLS,

    @EnumValue("Upgrade VM Image")
    UpgradeVmImage,

    @EnumValue("Upgrade Systemd")
    UpgradeSystemd,

    @EnumValue("Set Universe Helm3 Compatible")
    SetHelm3Compatible,

    @EnumValue("Set Universe's Backup Flag")
    setBackupFlag,

    @EnumValue("Set Universe's Key")
    setUniverseKey,

    @EnumValue("Toggle Universe's TLS State")
    ToggleTls,

    @EnumValue("Tls Configuration Update")
    TlsConfigUpdate,

    @EnumValue("Update Disk Size")
    UpdateDiskSize,

    @EnumValue("Create Cluster")
    CreateCluster,

    @EnumValue("Delete Cluster")
    DeleteCluster,

    @EnumValue("Create All Clusters")
    CreateAllClusters,

    @EnumValue("Update Primary Cluster")
    UpdatePrimaryCluster,

    @EnumValue("Update Read Only Cluster")
    UpdateReadOnlyCluster,

    @EnumValue("Create Read Only Cluster")
    CreateReadOnlyCluster,

    @EnumValue("Create Read Only Cluster")
    DeleteReadOnlyCluster,

    @EnumValue("Run YSQL Query in Universe")
    RunYsqlQuery,

    @EnumValue("Edit XCluster Configuration")
    EditXClusterConfig,

    @EnumValue("Delete XCluster Configuration")
    DeleteXClusterConfig,

    @EnumValue("Sync XCluster Configuration")
    SyncXClusterConfig,

    @EnumValue("Create Table")
    CreateTable,

    @EnumValue("Drop Table")
    DropTable,

    @EnumValue("Bulk Import Data into Table")
    BulkImport,

    @EnumValue("Create a Backup")
    CreateBackup,

    @EnumValue("Create a Multi Table Backup")
    CreateMultiTableBackup,

    @EnumValue("Restore from a Backup")
    RestoreBackup,

    @EnumValue("Delete Backups")
    DeleteBackups,

    @EnumValue("Stop a Backup")
    StopBackup,

    @EnumValue("Edit a Backup")
    EditBackup,

    @EnumValue("Retry a Universe Task")
    RetryTask,

    @EnumValue("Create Node Instance")
    CreateNodeInstance,

    @EnumValue("Delete Node Instance")
    DeleteNodeInstance,

    @EnumValue("Detached Node Instance Action")
    DetachedNodeInstanceAction,

    @EnumValue("Node Instance Action")
    NodeInstanceAction,

    @EnumValue("Delete a Backup Schedule")
    DeleteBackupSchedule,

    @EnumValue("Create User")
    CreateUser,

    @EnumValue("Change User Role")
    ChangeUserRole,

    @EnumValue("Update User Profile")
    UpdateUserProfile,

    @EnumValue("Delete User")
    DeleteUser
  }

  // An auto incrementing, user-friendly ID for the audit entry.
  @ApiModelProperty(
      value = "Audit UID",
      notes = "Automatically-incremented, user-friendly ID for the audit entry",
      accessMode = READ_ONLY)
  @Id
  @SequenceGenerator(name = "audit_id_seq", sequenceName = "audit_id_seq", allocationSize = 1)
  @GeneratedValue(strategy = GenerationType.SEQUENCE, generator = "audit_id_seq")
  private Long id;

  public Long getAuditID() {
    return this.id;
  }

  @ApiModelProperty(value = "User UUID", accessMode = READ_ONLY)
  @Constraints.Required
  @Column(nullable = false)
  private UUID userUUID;

  public UUID getUserUUID() {
    return this.userUUID;
  }

  @ApiModelProperty(value = "User Email", accessMode = READ_ONLY)
  @Column(nullable = true)
  private String userEmail;

  public String getUserEmail() {
    return this.userEmail;
  }

  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  @Constraints.Required
  @Column(nullable = false)
  private UUID customerUUID;

  public UUID getCustomerUUID() {
    return this.customerUUID;
  }

  // The task creation time.
  @CreatedTimestamp private final Date timestamp;

  public Date getTimestamp() {
    return this.timestamp;
  }

  @ApiModelProperty(value = "Audit UUID", accessMode = READ_ONLY, dataType = "Object")
  @Column(columnDefinition = "TEXT")
  @DbJson
  private JsonNode payload;

  public JsonNode getPayload() {
    return this.payload;
  }

  public void setPayload(JsonNode payload) {
    this.payload = payload;
    this.save();
  }

  @ApiModelProperty(
      value = "API call",
      example = "/api/v1/customers/<496fdea8-df25-11eb-ba80-0242ac130004>/providers",
      accessMode = READ_ONLY)
  @Constraints.Required
  @Column(columnDefinition = "TEXT", nullable = false)
  private String apiCall;

  public String getApiCall() {
    return this.apiCall;
  }

  @ApiModelProperty(value = "API method", example = "GET", accessMode = READ_ONLY)
  @Constraints.Required
  @Column(columnDefinition = "TEXT", nullable = false)
  private String apiMethod;

  public String getApiMethod() {
    return this.apiMethod;
  }

  @ApiModelProperty(value = "Target", example = "User", accessMode = READ_ONLY)
  @Enumerated(EnumType.STRING)
  private TargetType target;

  public TargetType getTarget() {
    return this.target;
  }

  public void setTarget(TargetType target) {
    this.target = target;
    this.save();
  }

  @ApiModelProperty(value = "Target UUID", accessMode = READ_ONLY)
  @Column(nullable = true)
  private UUID targetUUID;

  public UUID getTargetUUID() {
    return this.targetUUID;
  }

  @ApiModelProperty(value = "Action", example = "Create User", accessMode = READ_ONLY)
  @Enumerated(EnumType.STRING)
  private ActionType action;

  public ActionType getAction() {
    return this.action;
  }

  public void setAction(ActionType action) {
    this.action = action;
    this.save();
  }

  @ApiModelProperty(value = "Task UUID", accessMode = READ_ONLY)
  @Column(unique = true)
  private UUID taskUUID;

  public void setTaskUUID(UUID uuid) {
    this.taskUUID = uuid;
    this.save();
  }

  public UUID getTaskUUID() {
    return this.taskUUID;
  }

  public Audit() {
    this.timestamp = new Date();
  }

  public static final Finder<UUID, Audit> find = new Finder<UUID, Audit>(Audit.class) {};

  /**
   * Create new audit entry.
   *
   * @return Newly Created Audit table entry.
   */
  public static Audit create(
      Users user,
      String apiCall,
      String apiMethod,
      TargetType target,
      UUID targetUUID,
      ActionType action,
      JsonNode body,
      UUID taskUUID) {
    Audit entry = new Audit();
    entry.customerUUID = user.customerUUID;
    entry.userUUID = user.uuid;
    entry.userEmail = user.email;
    entry.apiCall = apiCall;
    entry.apiMethod = apiMethod;
    entry.target = target;
    entry.targetUUID = targetUUID;
    entry.action = action;
    entry.taskUUID = taskUUID;
    entry.payload = body;
    entry.save();
    return entry;
  }

  public static List<Audit> getAll(UUID customerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).findList();
  }

  public static Audit getFromTaskUUID(UUID taskUUID) {
    return find.query().where().eq("task_uuid", taskUUID).findOne();
  }

  public static Audit getOrBadRequest(UUID customerUUID, UUID taskUUID) {
    Customer.getOrBadRequest(customerUUID);
    Audit entry =
        find.query().where().eq("task_uuid", taskUUID).eq("customer_uuid", customerUUID).findOne();
    if (entry == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Task " + taskUUID + " does not belong to customer " + customerUUID);
    }
    return entry;
  }

  public static List<Audit> getAllUserEntries(UUID userUUID) {
    return find.query().where().eq("user_uuid", userUUID).findList();
  }

  public static void forEachEntry(Consumer<Audit> consumer) {
    find.query().findEach(consumer);
  }
}
