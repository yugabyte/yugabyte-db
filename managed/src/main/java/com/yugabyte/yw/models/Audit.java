// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.WhenCreated;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.SequenceGenerator;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

@ApiModel(description = "Audit logging for requests and responses")
@Entity
@Getter
@Setter
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

    @EnumValue("Customer Configuration")
    CustomerConfig,

    @EnumValue("KMS Configuration")
    KMSConfig,

    @EnumValue("Customer")
    Customer,

    @EnumValue("Release")
    Release,

    @EnumValue("Certificate")
    Certificate,

    @EnumValue("CustomCACertificate")
    CustomCACertificate,

    @EnumValue("Alert")
    Alert,

    @EnumValue("Alert Template Settings")
    AlertTemplateSettings,

    @EnumValue("Alert Template Variables")
    AlertTemplateVariables,

    @EnumValue("Alert Channel")
    AlertChannel,

    @EnumValue("Alert Channel Templates")
    AlertChannelTemplates,

    @EnumValue("Alert Destination")
    AlertDestination,

    @EnumValue("Maintenance Window")
    MaintenanceWindow,

    @EnumValue("Access Key")
    AccessKey,

    @EnumValue("Universe")
    Universe,

    @EnumValue("XCluster Configuration")
    XClusterConfig,

    @EnumValue("Disaster Recovery Configuration")
    DrConfig,

    @EnumValue("Table")
    Table,

    @EnumValue("Backup")
    Backup,

    @EnumValue("Customer Task")
    CustomerTask,

    @EnumValue("Node Instance")
    NodeInstance,

    @EnumValue("Platform Instance")
    PlatformInstance,

    @EnumValue("Schedule")
    Schedule,

    @EnumValue("User")
    User,

    @EnumValue("Logging Configuration")
    LoggingConfig,

    @EnumValue("Runtime Configuration Key")
    RuntimeConfigKey,

    @EnumValue("HA Configuration")
    HAConfig,

    @EnumValue("HA Backup")
    HABackup,

    @EnumValue("Scheduled Script")
    ScheduledScript,

    @EnumValue("Support Bundle")
    SupportBundle,

    @EnumValue("Telemetry Provider")
    TelemetryProvider,

    @EnumValue("GFlags")
    GFlags,

    @EnumValue("Hook")
    Hook,

    @EnumValue("Hook Scope")
    HookScope,

    @EnumValue("NodeAgent")
    NodeAgent,

    @EnumValue("CustomerLicense")
    CustomerLicense,

    @EnumValue("PerformanceRecommendation")
    PerformanceRecommendation,

    @EnumValue("PerformanceAdvisorSettings")
    PerformanceAdvisorSettings,

    @EnumValue("PerformanceAdvisorRun")
    PerformanceAdvisorRun,

    @EnumValue("Role")
    Role,

    @EnumValue("RoleBinding")
    RoleBinding,

    @EnumValue("OIDC Group Mapping")
    OIDCGroupMapping
  }

  public enum ActionType {
    @EnumValue("Set")
    Set,

    @EnumValue("Create")
    Create,

    @EnumValue("Edit")
    Edit,

    @EnumValue("Update")
    Update,

    @EnumValue("Delete")
    Delete,

    @EnumValue("Register")
    Register,

    @EnumValue("Refresh")
    Refresh,

    @EnumValue("Upload")
    Upload,

    @EnumValue("Upgrade")
    Upgrade,

    @EnumValue("Import")
    Import,

    @EnumValue("Pause")
    Pause,

    @EnumValue("Resume")
    Resume,

    @EnumValue("Restart")
    Restart,

    @EnumValue("Abort")
    Abort,

    @EnumValue("Retry")
    Retry,

    @EnumValue("Restore")
    Restore,

    @EnumValue("Alter")
    Alter,

    @EnumValue("Drop")
    Drop,

    @EnumValue("Stop")
    Stop,

    @EnumValue("Validate")
    Validate,

    @EnumValue("Acknowledge")
    Acknowledge,

    @EnumValue("Sync XCluster Configuration")
    SyncXClusterConfig,

    @EnumValue("Sync disaster recovery Configuration")
    SyncDrConfig,

    @EnumValue("Failover")
    Failover,

    @EnumValue("Switchover")
    Switchover,

    @EnumValue("Login")
    Login,

    @EnumValue("ApiLogin")
    ApiLogin,

    @EnumValue("Promote")
    Promote,

    @EnumValue("Bootstrap")
    Bootstrap,

    @EnumValue("Configure")
    Configure,

    @EnumValue("Update Options")
    UpdateOptions,

    @EnumValue("Update Load Balancer Config")
    UpdateLoadBalancerConfig,

    @EnumValue("Refresh Pricing")
    RefreshPricing,

    @EnumValue("Upgrade Software")
    UpgradeSoftware,

    @EnumValue("Finalize Upgrade")
    FinalizeUpgrade,

    @EnumValue("Rollback Upgrade")
    RollbackUpgrade,

    @EnumValue("Upgrade GFlags")
    UpgradeGFlags,

    @EnumValue("Create Telemetry Provider Config")
    CreateTelemetryConfig,

    @EnumValue("Delete Telemetry Provider Config")
    DeleteTelemetryConfig,

    @EnumValue("Upgrade Kubernetes Overrides")
    UpgradeKubernetesOverrides,

    @EnumValue("Upgrade Certs")
    UpgradeCerts,

    @EnumValue("Upgrade TLS")
    UpgradeTLS,

    @EnumValue("Upgrade VM Image")
    UpgradeVmImage,

    @EnumValue("Upgrade Systemd")
    UpgradeSystemd,

    @EnumValue("Reboot Universe")
    RebootUniverse,

    @EnumValue("Resize Node")
    ResizeNode,

    @EnumValue("Add Metrics")
    AddMetrics,

    @EnumValue("Create Kubernetes")
    CreateKubernetes,

    @EnumValue("Setup Docker")
    SetupDocker,

    @EnumValue("Retrieve KMS Key")
    RetrieveKmsKey,

    @EnumValue("Remove KMS Key Reference History")
    RemoveKmsKeyReferenceHistory,

    @EnumValue("Upsert Customer Features")
    UpsertCustomerFeatures,

    @EnumValue("Create Self Signed Certificate")
    CreateSelfSignedCert,

    @EnumValue("Update Empty Customer Certificate")
    UpdateEmptyCustomerCertificate,

    @EnumValue("Get Client's Root Certificate")
    GetRootCertificate,

    @EnumValue("Add Client Certificate")
    AddClientCertificate,

    @EnumValue("Set DB Credentials")
    SetDBCredentials,

    @EnumValue("Create User in DB")
    CreateUserInDB,

    @EnumValue("Create Restricted User in DB")
    CreateRestrictedUserInDB,

    @EnumValue("Drop User in DB")
    DropUserInDB,

    @EnumValue("Set Universe Helm3 Compatible")
    SetHelm3Compatible,

    @EnumValue("Set Universe's Backup Flag")
    SetBackupFlag,

    @EnumValue("Set Universe Key")
    SetUniverseKey,

    @EnumValue("Reset Universe Version")
    ResetUniverseVersion,

    @EnumValue("Configure Universe Alert")
    ConfigUniverseAlert,

    @EnumValue("Toggle Universe's TLS State")
    ToggleTls,

    @EnumValue("Modify Universe's Audit Logging Config")
    ModifyAuditLogging,

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

    @EnumValue("Bulk Import Data into Table")
    BulkImport,

    @EnumValue("Create Backup")
    CreateBackup,

    @EnumValue("Restore Backup")
    RestoreBackup,

    @EnumValue("Create Single Table Backup")
    CreateSingleTableBackup,

    @EnumValue("Create a Multi Table Backup")
    CreateMultiTableBackup,

    @EnumValue("Create Backup Schedule")
    CreateBackupSchedule,

    @EnumValue("Create PITR Config")
    CreatePitrConfig,

    @EnumValue("Restore Snapshot Schedule")
    RestoreSnapshotSchedule,

    @EnumValue("Delete PITR Config")
    DeletePitrConfig,

    @EnumValue("Edit Backup Schedule")
    EditBackupSchedule,

    @EnumValue("Start Periodic Backup")
    StartPeriodicBackup,

    @EnumValue("Stop Periodic Backup")
    StopPeriodicBackup,

    @EnumValue("Detached Node Instance Action")
    DetachedNodeInstanceAction,

    @EnumValue("Node Instance Action")
    NodeInstanceAction,

    @EnumValue("Delete a Backup Schedule")
    DeleteBackupSchedule,

    @EnumValue("Change User Role")
    ChangeUserRole,

    @EnumValue("Change User Password")
    ChangeUserPassword,

    @EnumValue("Set Security")
    SetSecurity,

    @EnumValue("Generate API Token")
    GenerateApiToken,

    @EnumValue("Reset Slow Queries")
    ResetSlowQueries,

    @EnumValue("External Script Schedule")
    ExternalScriptSchedule,

    @EnumValue("Stop Scheduled Script")
    StopScheduledScript,

    @EnumValue("Update Scheduled Script")
    UpdateScheduledScript,

    @EnumValue("Create Instance Type")
    CreateInstanceType,

    @EnumValue("Delete Instance Type")
    DeleteInstanceType,

    @EnumValue("Get Universe Resources")
    GetUniverseResources,

    @EnumValue("Third-party Software Upgrade")
    ThirdpartySoftwareUpgrade,

    @EnumValue("Create TableSpaces")
    CreateTableSpaces,

    @EnumValue("Create Hook")
    CreateHook,

    @EnumValue("Delete Hook")
    DeleteHook,

    @EnumValue("Update Hook")
    UpdateHook,

    @EnumValue("Create Hook Scope")
    CreateHookScope,

    @EnumValue("Delete Hook Scope")
    DeleteHookScope,

    @EnumValue("Add Hook to Hook Scope")
    AddHook,

    @EnumValue("Remove Hook from Hook Scope")
    RemoveHook,

    @EnumValue("Rotate AccessKey")
    RotateAccessKey,

    @EnumValue("Create And Rotate Access Key")
    CreateAndRotateAccessKey,

    @EnumValue("Run Hook")
    RunHook,

    @EnumValue("Run API Triggered Tasks")
    RunApiTriggeredHooks,

    @EnumValue("Add Node Agent")
    AddNodeAgent,

    @EnumValue("Update Node Agent")
    UpdateNodeAgent,

    @EnumValue("Delete Node Agent")
    DeleteNodeAgent,

    @EnumValue("Disable Ybc")
    DisableYbc,

    @EnumValue("Upgrade Ybc")
    UpgradeYbc,

    @EnumValue("Install Ybc")
    InstallYbc,

    @EnumValue("Upgrade Ybc GFlags")
    UpgradeYbcGFlags,

    @EnumValue("Set YB-Controller throttle params")
    SetThrottleParams,

    @EnumValue("Create Image Bundle")
    CreateImageBundle,

    @EnumValue("Delete Image Bundle")
    DeleteImageBundle,

    @EnumValue("Edit Image Bundle")
    EditImageBundle,

    @EnumValue("Export")
    Export,

    @EnumValue("Delete Universe Metadata")
    DeleteMetadata,

    @EnumValue("Unlock Universe")
    Unlock,

    @EnumValue("Sync Universe with LDAP server")
    LdapUniverseSync,

    @EnumValue("Update Universe's Proxy Configuration")
    UpdateProxyConfig
  }

  // An auto incrementing, user-friendly ID for the audit entry.
  @ApiModelProperty(
      value = "Audit UID",
      notes = "Automatically-incremented, user-friendly ID for the audit entry",
      accessMode = READ_ONLY)
  @Id
  @SequenceGenerator(name = "audit_id_seq", sequenceName = "audit_id_seq", allocationSize = 1)
  @GeneratedValue(strategy = GenerationType.SEQUENCE, generator = "audit_id_seq")
  @JsonProperty("auditID")
  private Long id;

  @ApiModelProperty(value = "User UUID", accessMode = READ_ONLY)
  @Constraints.Required
  @Column(nullable = false)
  private UUID userUUID;

  @ApiModelProperty(value = "User Email", accessMode = READ_ONLY)
  @Column(nullable = true)
  private String userEmail;

  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  @Constraints.Required
  @Column(nullable = false)
  private UUID customerUUID;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(
      value = "The task creation time.",
      accessMode = READ_ONLY,
      example = "2022-12-12T13:07:18Z")
  @WhenCreated
  private final Date timestamp;

  @ApiModelProperty(value = "Audit UUID", accessMode = READ_ONLY, dataType = "Object")
  @Column(columnDefinition = "TEXT")
  @DbJson
  private JsonNode payload;

  @ApiModelProperty(value = "Additional Details", accessMode = READ_ONLY, dataType = "Object")
  @Column(columnDefinition = "TEXT")
  @DbJson
  private JsonNode additionalDetails;

  @ApiModelProperty(
      value = "API call",
      example = "/api/v1/customers/<496fdea8-df25-11eb-ba80-0242ac130004>/providers",
      accessMode = READ_ONLY)
  @Constraints.Required
  @Column(columnDefinition = "TEXT", nullable = false)
  private String apiCall;

  @ApiModelProperty(value = "API method", example = "GET", accessMode = READ_ONLY)
  @Constraints.Required
  @Column(columnDefinition = "TEXT", nullable = false)
  private String apiMethod;

  @ApiModelProperty(value = "Target", example = "User", accessMode = READ_ONLY)
  @Enumerated(EnumType.STRING)
  private TargetType target;

  @ApiModelProperty(value = "Target ID", accessMode = READ_ONLY)
  @Column(nullable = true)
  private String targetID;

  @ApiModelProperty(value = "Action", example = "Create User", accessMode = READ_ONLY)
  @Enumerated(EnumType.STRING)
  private ActionType action;

  @ApiModelProperty(value = "Task UUID", accessMode = READ_ONLY)
  @Column(unique = true)
  private UUID taskUUID;

  @ApiModelProperty(value = "User IP Address", accessMode = READ_ONLY)
  @Column(nullable = true)
  private String userAddress;

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
      String targetID,
      ActionType action,
      JsonNode body,
      UUID taskUUID,
      JsonNode details,
      String userAddress) {
    Audit entry = new Audit();
    entry.setCustomerUUID(user.getCustomerUUID());
    entry.setUserUUID(user.getUuid());
    entry.setUserEmail(user.getEmail());
    entry.setApiCall(apiCall);
    entry.setApiMethod(apiMethod);
    entry.setTarget(target);
    entry.setTargetID(targetID);
    entry.setAction(action);
    entry.setTaskUUID(taskUUID);
    entry.setPayload(body);
    entry.setAdditionalDetails(details);
    entry.setUserAddress(userAddress);
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
}
