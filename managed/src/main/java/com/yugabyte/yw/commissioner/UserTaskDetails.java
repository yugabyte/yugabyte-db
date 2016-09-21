package com.yugabyte.yw.commissioner;

import java.util.ArrayList;
import java.util.List;

/**
 * Class that encapsulates the user task details.
 */
public class UserTaskDetails {
  // The various user facing subtasks.
  public static enum SubTaskType {
    // Ignore this subtask and do not display it to the user.
    Invalid,

    // Deploying machines in the desired cloud, fetching information (ip address, etc) of these
    // newly deployed machines, etc.
    Provisioning,

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
  }

  // The user facing state of the various subtasks.
  public static enum SubTaskState {
    // The subtask has not yet started/
    Initializing,

    // The subtask is currently running.
    Running,

    // The subtask has succeeded.
    Success,

    // The subtask has failed.
    Failure,

    // The subtask state is no longer valid, for example the user task has failed before the subtask
    // got a chance to run.
    Unknown
  }

  public List<SubTaskDetails> taskDetails;

  // TODO: all the strings in this method should move into conf files eventually.
  public static SubTaskDetails createSubTask(SubTaskType subTaskType) {
    String title = null;
    String description = null;
    if (subTaskType == SubTaskType.Provisioning) {
      title = "Provisioning";
      description = "Deploying machines of the required config into the desired cloud and" +
                    " fetching information about them.";
    } else if (subTaskType == SubTaskType.InstallingSoftware) {
      title = "Installing software";
      description = "Configuring mount points, setting up the various directories and installing" +
                    " the YugaByte software on the newly provisioned nodes.";
    } else if (subTaskType == SubTaskType.ConfigureUniverse) {
      title = "Configuring the universe";
      description = "Creating and populating the universe config, waiting for the various" +
                    " machines to discover one another.";
    } else if (subTaskType == SubTaskType.WaitForDataMigration) {
      title = "Waiting for data migration";
      description = "Waiting for the data to get copied into the new set of machines to achieve" +
                    " the desired configuration.";
    } else if (subTaskType == SubTaskType.RemovingUnusedServers) {
      title = "Removing servers no longer used";
      description = "Removing servers that are no longer needed once the configuration change has" +
                    " been successfully completed";
    }

    if (title == null) {
      return null;
    }
    return new SubTaskDetails(title, description);
  }

  public UserTaskDetails() {
    taskDetails = new ArrayList<SubTaskDetails>();
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
    private SubTaskState state;

    private SubTaskDetails(String title, String description) {
      this.title = title;
      this.description = description;
      this.state = SubTaskState.Unknown;
    }

    public void setState(SubTaskState state) {
      this.state = state;
    }

    public String getTitle() { return title; }

    public String getDescription() { return description; }

    public String getState() { return state.toString(); }
  }
}
