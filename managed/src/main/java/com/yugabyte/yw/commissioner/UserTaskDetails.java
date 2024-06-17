// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.TaskInfo;
import java.util.ArrayList;
import java.util.List;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** Class that encapsulates the user task details. */
public class UserTaskDetails {
  public static final Logger LOG = LoggerFactory.getLogger(UserTaskDetails.class);

  // The various groupings of user facing subtasks.
  public enum SubTaskGroupType {
    // Used for parent tasks which could have own details/errors. Only for UI/API
    // purposes, not stored in DB.
    Preparation,

    // Ignore this subtask and do not display it to the user.
    Invalid,

    // Perform preflight checks to determine if the node is ready to be configured or provisioned.
    PreflightChecks,

    // Deploying machines in the desired cloud, fetching information (ip address, etc) of these
    // newly deployed machines, etc.
    Provisioning,

    // Running software upgrade on Yugabyte clusters.
    UpgradingSoftware,

    // Rolling back software upgrade on Yugabyte Clusters.
    RollingBackSoftware,

    // Finalizing Yugabyte db software upgrade on Yugabyte clusters.
    FinalizingUpgrade,

    // Download YB software locally but not install it.
    DownloadingSoftware,

    // Configure the mount points and the directories, install the desired version of YB software,
    // add the control scripts to start and stop daemons, setup monitoring, etc.
    InstallingSoftware,

    // Start the masters to create a new universe configuration, wait for leader elections, set
    // placement info, wait for the tservers to start up, etc.
    ConfigureUniverse,

    // Querying the upstream LDAP server
    QueryLdapServer,

    // Db-Ldap Sync
    DbLdapSync,

    // Increasing disk size
    ResizingDisk,

    // Change instance type
    ChangeInstanceType,

    // Migrating data from one set of nodes to another.
    WaitForDataMigration,

    // Remove old, unused servers.
    RemovingUnusedServers,

    // Updating GFlags
    UpdatingGFlags,

    // Updating YBC GFlags
    UpdatingYbcGFlags,

    // Bootstrap Cloud
    BootstrappingCloud,

    // Bootstrapping Region
    BootstrappingRegion,

    // Checkpointing a table on the source universe to set up xCluster replication.
    BootstrappingProducer,

    // Creating Access Key
    CreateAccessKey,

    // Deleting all the xCluster replications and cleaning up their states on the universes.
    DeleteXClusterReplication,

    // Deleting DR config.
    DeleteDrConfig,

    // Rotate access key to all nodes of a universe
    RotateAccessKey,

    // Initializing Cloud Metadata
    InitializeCloudMetadata,

    // Cleanup Cloud
    CleanupCloud,

    // Creating Table
    CreatingTable,

    // Creating Tablespaces
    CreatingTablespaces,

    // Importing Data
    ImportingData,

    // Delete Node
    DeletingNode,

    // Stopping Node
    StoppingNode,

    // Starting Node
    StartingNode,

    // Pausing universe
    PauseUniverse,

    // Resuming universe
    ResumeUniverse,

    // Start master, tserver and yb-controller processes on a node
    StartingNodeProcesses,

    // Stop master, tserver and yb-controller processes on a node
    StoppingNodeProcesses,

    // Adding a node.
    AddingNode,

    // Removing a node.
    RemovingNode,

    // Releasing the backing IaaS instance.
    ReleasingInstance,

    // Deleting Table
    DeletingTable,

    // Deleting Backup
    DeletingBackup,

    // Creating a backup
    CreatingBackup,

    // Creating Table Backup
    CreatingTableBackup,

    // Creating Universe Backup
    CreatingUniverseBackup,

    // Restoring Table Backup
    RestoringTableBackup,

    // Restoring Backup
    RestoringBackup,

    // Create Namespace for Kubectl.
    CreateNamespace,

    // Apply Pull Secret.
    ApplySecret,

    // Initialize Kubernetes cluster
    HelmInit,

    // Install YugaByte helm chart.
    HelmInstall,

    // Update number of nodes k8s
    UpdateNumNodes,

    // Delete YugaByte helm chart.
    HelmDelete,

    // Delete Kubernetes volumes created by helm chart.
    KubernetesVolumeDelete,

    // Delete Kubernetes Volumes for a shell mode master before creating it.
    KubernetesVolumeDeleteMasterShellMode,

    // Delete kubernetes namespace
    KubernetesNamespaceDelete,

    // Fetch the Kubernetes pod information.
    KubernetesPodInfo,

    // Delete all server type pods
    DeleteAllServerTypePods,

    // Wait for Kubernetes pod deployment
    KubernetesWaitForPod,

    // Wait for Kubernetes to have correct number of pods.
    KubernetesCheckNumPod,

    // Upgrade existing helm chart.
    HelmUpgrade,

    // Upgrade pod in Kubernetes.
    KubernetesUpgradePod,

    // Copy package to YBC
    KubernetesCopyPackage,

    // Perform YBC Action
    KubernetesYbcAction,

    // Run the initdb script in a tserver pod. (Deprecated)
    KubernetesInitYSQL,

    // Start master process on a node
    StartingMasterProcess,

    // Rotate Node Certs.
    RotatingCert,

    // Upgrade to Systemd.
    SystemdUpgrade,

    // Add certificates and toggle TLS gflags
    ToggleTls,

    // Configure DB Apis
    ConfigureDBApis,

    // Rebooting the node.
    RebootingNode,

    // Hard rebooting (stop/start) the node.
    HardRebootingNode,

    // Running custom hooks
    RunningHooks,

    // Updating Packages
    UpdatePackage,

    // Upgrading Yb-Controller
    UpgradingYbc,

    // Updating kubernetes overrides.
    UpdatingKubernetesOverrides,

    // Fetch PVC and StorageClass information
    KubernetesVolumeInfo,

    // Install Third Party Packages
    InstallingThirdPartySoftware,

    // Promote Auto Flags
    PromoteAutoFlags,

    // Rollback Auto Flags
    RollbackAutoFlags,

    // Validate configurations.
    ValidateConfigurations,

    // Manage Otel Collector.
    ManageOtelCollector,

    // Validating nodes.
    ValidatingNode,

    // OS Patching.
    OSPatching,

    // Update Proxy Config
    UpdateProxyConfig
  }

  public List<SubTaskDetails> taskDetails;

  // TODO: all the strings in this method should move into conf files eventually.
  public static SubTaskDetails createSubTask(SubTaskGroupType subTaskGroupType) {
    String title;
    String description;
    switch (subTaskGroupType) {
      case Preparation:
        title = "Action preparation";
        description = "Preparing to execute a selected action.";
        break;
      case PreflightChecks:
        title = "Preflight Checks";
        description =
            "Perform preflight checks to determine if node is ready"
                + " to be provisioned/configured.";
        break;
      case Provisioning:
        title = "Provisioning";
        description =
            "Deploying machines of the required config into the desired cloud and"
                + " fetching information about them.";
        break;
      case UpgradingSoftware:
        title = "Upgrading software";
        description = "Upgrading YugaByte software on existing clusters.";
        break;
      case RollingBackSoftware:
        title = "Rolling back Software";
        description = "Rolling back software upgrade on existing clusters.";
        break;
      case FinalizingUpgrade:
        title = "Finalizing upgrade";
        description = "Finalizing Yugabyte DB Software version upgrade on universe";
        break;
      case InstallingSoftware:
        title = "Installing software";
        description =
            "Configuring mount points, setting up the various directories and installing"
                + " the YugaByte software on the newly provisioned nodes.";
        break;
      case ConfigureUniverse:
        title = "Configuring the universe";
        description =
            "Creating and populating the universe config, waiting for the various"
                + " machines to discover one another.";
        break;
      case QueryLdapServer:
        title = "Querying LDAP Server";
        description = "Querying the LDAP Server for user-group mapping";
        break;
      case DbLdapSync:
        title = "Syncing DB roles with LDAP groups";
        description =
            "Performing a manual sync of user groups and roles between the Universe DB nodes and"
                + " the upstream LDAP Server.";
        break;
      case ResizingDisk:
        title = "Increasing disk size";
        description = "Increasing disk size on live nodes to the size intended by the user";
        break;
      case SystemdUpgrade:
        title = "Upgrading to Systemd";
        description = "Upgrading Cron Job to Systemd for all nodes in the universe.";
        break;
      case ChangeInstanceType:
        title = "Changing instance type";
        description = "Change the instance type of all the nodes in the universe";
        break;
      case WaitForDataMigration:
        title = "Waiting for data migration";
        description =
            "Waiting for the data to get copied into the new set of machines to achieve"
                + " the desired configuration.";
        break;
      case RemovingUnusedServers:
        title = "Removing servers no longer used";
        description =
            "Removing servers that are no longer needed once the configuration change has"
                + " been successfully completed";
        break;
      case DownloadingSoftware:
        title = "Downloading software";
        description = "Downloading the YugaByte software on provisioned nodes.";
        break;
      case UpdatingGFlags:
        title = "Updating gflags";
        description = "Updating GFlags on provisioned nodes.";
        break;
      case UpdatingYbcGFlags:
        title = "Updating YBC GFlags";
        description = "Updating YBC GFlags on provisioned nodes.";
        break;
      case UpdatingKubernetesOverrides:
        title = "Updating kubernetes overrides";
        description = "Updating kubernetes overrides on kubernetes pods.";
        break;
      case BootstrappingCloud:
        title = "Bootstrapping Cloud";
        description = "Set up AccessKey, Region, and Provider for a given cloud Provider.";
        break;
      case BootstrappingRegion:
        title = "Bootstrapping Region";
        description = "Set up AccessKey, Region, and Provider for a given cloud Provider.";
        break;
      case BootstrappingProducer:
        title = "Bootstrapping Source Universe";
        description = "Creating a checkpoint on the source universe.";
        break;
      case CreateAccessKey:
        title = "Creating AccessKey";
        description = "Set up AccessKey in the given Provider Vault";
        break;
      case DeleteXClusterReplication:
        title = "Deleting XCluster Replication";
        description =
            "Deleting xCluster replications and cleaning up their corresponding states "
                + "on the participating universes.";
        break;
      case DeleteDrConfig:
        title = "Deleting Dr Config";
        description = "Deleting the disaster recovery config.";
        break;
      case InitializeCloudMetadata:
        title = "Initializing Cloud Metadata";
        description = "Initialize Instance Pricing and Zone Metadata from Cloud Provider";
        break;
      case CleanupCloud:
        title = "Cleaning Up Cloud";
        description = "Remove AccessKey, Region, and Provider for a given cloud Provider.";
        break;
      case CreatingTable:
        title = "Creating Table";
        description = "Create a table.";
        break;
      case CreatingTablespaces:
        title = "Creating Tablespaces";
        description = "Create tablespaces.";
        break;
      case ImportingData:
        title = "Importing Data";
        description = "Import a large amount of data into a table";
        break;
      case DeletingNode:
        title = "Deleting Node";
        description = "Remove Node entry from Universe details";
        break;
      case StoppingNode:
        title = "Stopping Node";
        description = "Waiting for node to stop.";
        break;
      case StartingNode:
        title = "Starting Node";
        description = "Waiting for node to start.";
        break;
      case StartingNodeProcesses:
        title = "Starting Node processes";
        description = "Waiting for node to start either tserver or master process.";
        break;
      case AddingNode:
        title = "Adding a node";
        description = "Add a node to universe and start tserver (and master, if required).";
        break;
      case RemovingNode:
        title = "Removing a node";
        description = "Remove a node from universe.";
        break;
      case ReleasingInstance:
        title = "Releasing the instance back to the IaaS.";
        description = "Releasing the instance from universe.";
        break;
      case StoppingNodeProcesses:
        title = "Stopping Node Processes";
        description = "Stopping tserver and master processes on the node.";
        break;
      case DeletingTable:
        title = "Deleting Table";
        description = "Delete an existing table from a universe.";
        break;
      case DeletingBackup:
        title = "Deleting Backup";
        description = "Delete an existing backup of a universe.";
        break;
      case CreatingBackup:
        title = "Creating Backup";
        description = "Creating backup for either a keyspace or a set of tables.";
        break;
      case CreatingTableBackup:
        title = "Creating Table Backup";
        description = "Creating backup for a table.";
        break;
      case CreatingUniverseBackup:
        title = "Creating Universe Backup";
        description = "Creating backup for a Universe.";
        break;
      case RestoringTableBackup:
        title = "Restoring Table Backup";
        description = "Restoring from a backup.";
        break;
      case RestoringBackup:
        title = "Restoring Backup";
        description = "Restoring from a backup.";
        break;
      case HelmInit:
        title = "Kubernetes Helm Charts initialize";
        description = "Initialize Kubernetes Helm Charts";
        break;
      case HelmInstall:
        title = "Kubernetes Helm Charts Install";
        description = "Install Kubernetes Helm Charts";
        break;
      case HelmUpgrade:
        title = "Kubernetes Helm Charts Update";
        description = "Update Kubernetes Helm Charts";
        break;
      case KubernetesPodInfo:
        title = "Fetch Kubernetes Pods Information";
        description = "Fetch Kubernetes Pods Information";
        break;
      case HelmDelete:
        title = "Delete Kubernetes Helm Charts";
        description = "Delete Kubernetes Helm Charts";
        break;
      case KubernetesVolumeDelete:
        title = "Delete Kubernetes Volumes";
        description = "Delete Kubernetes Volumes";
        break;
      case KubernetesVolumeDeleteMasterShellMode:
        title = "Delete Kubernetes Volumes for a master before creating it in shell mode";
        description = "Delete Kubernetes Volumes for master pod before creating it in shell mode";
        break;
      case KubernetesNamespaceDelete:
        title = "Delete Kubernetes Namespace";
        description = "Delete Kubernetes Namespace";
        break;
      case DeleteAllServerTypePods:
        title = "Delete all pods of server type in AZ";
        description = "Delete all pods of server type in AZ";
      case KubernetesWaitForPod:
        title = "Wait for Kubernetes pod to run";
        description = "Wait for Kubernetes pod to run";
        break;
      case KubernetesCheckNumPod:
        title = "Wait for Kubernetes to have correct number of pods.";
        description = "Wait for Kubernetes to have correct number of pods.";
        break;
      case KubernetesUpgradePod:
        title = "Upgrade Kubernetes Pod";
        description = "Upgrade Kubernetes Pod";
        break;
      case KubernetesCopyPackage:
        title = "Copy Package to Kubernetes Pod";
        description = "Copy Package to Kubernetes Pod";
        break;
      case KubernetesYbcAction:
        title = "Perform YBC Action in the Container";
        description = "Perform YBC Action in the Container";
        break;
      case KubernetesInitYSQL:
        title = "Initialize YSQL in Kubernetes Universe";
        description = "Initialize YSQL in Kubernetes Universe";
        break;
      case StartingMasterProcess:
        title = "Starting Master Process";
        description = "Waiting for node to start the master process.";
        break;
      case RotatingCert:
        title = "Rotating Cert";
        description = "Changing certs.";
        break;
      case CreateNamespace:
        title = "Creating Namespace";
        description = "Create namespace for Kubectl.";
        break;
      case ApplySecret:
        title = "Applying Pull Secret";
        description = "Apply Pull Secret.";
        break;
      case UpdateNumNodes:
        title = "Updating number of nodes";
        description = "Update number of nodes.";
        break;
      case PauseUniverse:
        title = "Pause Universe";
        description = "Pause the universe.";
        break;
      case ResumeUniverse:
        title = "Resume Universe";
        description = "Resume the universe.";
        break;
      case ToggleTls:
        title = "Toggle TLS";
        description = "Add certificates and toggle TLS gflags";
        break;
      case ConfigureDBApis:
        title = "Configure DB APIs";
        description = "Configuring DB APIs";
        break;
      case RotateAccessKey:
        title = "Rotate Access Key";
        description = "Rotate the access key for a universe";
        break;
      case RebootingNode:
        title = "Rebooting Node";
        description = "Rebooting node";
        break;
      case HardRebootingNode:
        title = "Hard Rebooting Node";
        description = "Hard rebooting node";
        break;
      case RunningHooks:
        title = "Running Hooks";
        description = "Run custom hooks";
        break;
      case UpdatePackage:
        title = "Update Packages";
        description = "Updating packages installed on the nodes";
        break;
      case UpgradingYbc:
        title = "Upgrading Yb-controller";
        description = "Upgrading yb-controller on each node";
        break;
      case KubernetesVolumeInfo:
        title = "Fetching Kubernetes Volume information";
        description = "Fetching Volume and storage information";
        break;
      case InstallingThirdPartySoftware:
        title = "Install Third Party Software Packages";
        description = "Installing Third party Software packages";
        break;
      case PromoteAutoFlags:
        title = "Promote Auto flags";
        description = "Promote Auto flags for a universe";
        break;
      case RollbackAutoFlags:
        title = "Rollback Auto flags";
        description = "Rollback Auto flags for a universe";
        break;
      case ValidateConfigurations:
        title = "Validating configurations";
        description = "Validating configurations before proceeding";
        break;
      case ManageOtelCollector:
        title = "Managing OpenTelemetry Collector";
        description = "Managing OpenTelemetry Collector";
        break;
      case ValidatingNode:
        title = "Validating node status";
        description = "Validating node's current state before proceeding";
        break;
      case OSPatching:
        title = "OS Patching";
        description = "Performing OS Patching on the universe nodes";
        break;
      case UpdateProxyConfig:
        title = "Updating Proxy config";
        description = "Updating Proxy Config for Universe nodes";
        break;
      default:
        LOG.warn("UserTaskDetails: Missing SubTaskDetails for : {}", subTaskGroupType);
        return null;
    }
    return new SubTaskDetails(title, description);
  }

  public UserTaskDetails() {
    taskDetails = new ArrayList<>();
  }

  public void add(SubTaskDetails subtaskDetails) {
    taskDetails.add(subtaskDetails);
  }

  public static class SubTaskDetails {
    // User facing title.
    private String title;

    // User facing short description.
    private String description;

    // The state of the task.
    private TaskInfo.State state;

    // Extra task details about a subtask like progress in tablet movement.
    public List<JsonNode> extraDetails;

    private SubTaskDetails(String title, String description) {
      this.title = title;
      this.description = description;
      this.state = TaskInfo.State.Unknown;
      this.extraDetails = new ArrayList<>();
    }

    public void setState(TaskInfo.State state) {
      this.state = state;
    }

    public String getTitle() {
      return title;
    }

    public String getDescription() {
      return description;
    }

    public TaskInfo.State getState() {
      return state;
    }

    public void populateDetails(JsonNode data) {
      if (data != null) {
        this.extraDetails.add(data);
      }
    }
  }
}
