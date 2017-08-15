package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.models.TaskInfo;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;

/**
 * Class that encapsulates the user task details.
 */
public class UserTaskDetails {
  public static final Logger LOG = LoggerFactory.getLogger(UserTaskDetails.class);

  // The various groupings of user facing subtasks.
  public enum SubTaskGroupType {
    // Ignore this subtask and do not display it to the user.
    Invalid,

    // Deploying machines in the desired cloud, fetching information (ip address, etc) of these
    // newly deployed machines, etc.
    Provisioning,

    // Download YB software locally but not install it.
    DownloadingSoftware,

    // Configure the mount points and the directories, install the desired version of YB software,
    // add the control scripts to start and stop daemons, setup monitoring, etc.
    InstallingSoftware,

    // Start the masters to create a new universe configuration, wait for leader elections, set
    // placement info, wait for the tservers to start up, etc.
    ConfigureUniverse,

    // Migrating data from one set of nodes to another.
    WaitForDataMigration,

    // Remove old, unused servers.
    RemovingUnusedServers,

    // Updating GFlags
    UpdatingGFlags,

    // Bootstrap Cloud
    BootstrappingCloud,

    // Cleanup Cloud
    CleanupCloud,

    // Creating Table
    CreatingTable,

    // Importing Data
    ImportingData
  }

  public List<SubTaskDetails> taskDetails;

  // TODO: all the strings in this method should move into conf files eventually.
  public static SubTaskDetails createSubTask(SubTaskGroupType subTaskGroupType) {
    String title;
    String description;
    switch (subTaskGroupType) {
      case Provisioning:
        title = "Provisioning";
        description = "Deploying machines of the required config into the desired cloud and" +
            " fetching information about them.";
        break;
      case InstallingSoftware:
        title = "Installing software";
        description = "Configuring mount points, setting up the various directories and installing" +
            " the YugaByte software on the newly provisioned nodes.";
        break;
      case ConfigureUniverse:
        title = "Configuring the universe";
        description = "Creating and populating the universe config, waiting for the various" +
            " machines to discover one another.";
        break;
      case WaitForDataMigration:
        title = "Waiting for data migration";
        description = "Waiting for the data to get copied into the new set of machines to achieve" +
            " the desired configuration.";
        break;
      case RemovingUnusedServers:
        title = "Removing servers no longer used";
        description = "Removing servers that are no longer needed once the configuration change has" +
            " been successfully completed";
        break;
      case DownloadingSoftware:
        title = "Downloading software";
        description = "Downloading the YugaByte software on provisioned nodes.";
        break;
      case UpdatingGFlags:
        title = "Updating gflags";
        description = "Updating GFlags on provisioned nodes.";
        break;
      case BootstrappingCloud:
        title = "Bootstrapping Cloud";
        description = "Set up AccessKey, Region, and Provider for a given cloud Provider.";
        break;
      case CleanupCloud:
        title = "Cleaning Up Cloud";
        description = "Remove AccessKey, Region, and Provider for a given cloud Provider.";
        break;
      case CreatingTable:
        title = "Creating Table";
        description = "Create a table.";
        break;
      case ImportingData:
        title = "Importing Data";
        description = "Import a large amount of data into a table";
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

    private SubTaskDetails(String title, String description) {
      this.title = title;
      this.description = description;
      this.state = TaskInfo.State.Unknown;
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

    public String getState() {
      return state.toString();
    }
  }
}
