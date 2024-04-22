/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 */

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.ldapsync.DbLdapSync;
import com.yugabyte.yw.commissioner.tasks.subtasks.ldapsync.QueryLdapServer;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.LdapUnivSyncFormData;
import com.yugabyte.yw.forms.UniverseTaskParams;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class LdapUnivSync extends UniverseTaskBase {
  private final HashMap<String, List<String>> userToGroup = new HashMap<>();
  private final List<String> ldapGroups = new ArrayList<>();

  @Inject
  protected LdapUnivSync(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public LdapUnivSyncFormData ldapUnivSyncFormData;
    public HashMap<String, List<String>> userToGroup;
    public List<String> ldapGroups;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  public SubTaskGroup createQueryLdapServerTask(LdapUnivSync.Params taskParams) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("QueryLdap");
    QueryLdapServer task = createTask(QueryLdapServer.class);
    task.initialize(taskParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createDbLdapSyncTask(LdapUnivSync.Params taskParams) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DbLdapSync");
    DbLdapSync task = createTask(DbLdapSync.class);
    task.initialize(taskParams);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  @Override
  public void run() {
    log.info("Started {} task for uuid={}", getName(), taskParams().getUniverseUUID());
    taskParams().userToGroup = userToGroup;
    taskParams().ldapGroups = ldapGroups;

    try {
      lockAndFreezeUniverseForUpdate(-1, null /* Txn callback */);

      // queryLdap
      createQueryLdapServerTask(taskParams()).setSubTaskGroupType(SubTaskGroupType.QueryLdapServer);

      // query the db, compute diff and apply changes
      createDbLdapSyncTask(taskParams()).setSubTaskGroupType(SubTaskGroupType.DbLdapSync);

      // Marks update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Exception t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);

      String errorMsg =
          String.format("Error executing task %s with error= %s. %s", getName(), t.getMessage(), t);
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    } finally {
      // Unlock the universe.
      unlockUniverseForUpdate(taskParams().getUniverseUUID());
    }
  }
}
