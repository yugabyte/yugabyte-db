/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;

public class SetUniverseKey extends UniverseTaskBase {
    public static final Logger LOG = LoggerFactory.getLogger(SetUniverseKey.class);

    @Override
    public void run() {
        LOG.info("Started {} task.", getName());
        try {
            String encryptionKeyFilePath = taskParams().encryptionKeyFilePath;

            // Create the task list sequence.
            subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

            // Update the universe DB with the update to be performed and set the
            // 'updateInProgress' flag to prevent other updates from happening.
            lockUniverseForUpdate(taskParams().expectedUniverseVersion);

            // Copy the key file over to master nodes
            createCopyEncryptionKeyFileTask();

            // Enable encryption-at-rest if key file is passed in
            createEnableEncryptionAtRestTask(encryptionKeyFilePath)
                    .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

            createDisableEncryptionAtRestTask(encryptionKeyFilePath)
                    .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

            // Update the universe model to reflect encryption is now enabled
            writeEncryptionIntentToUniverse(
                    encryptionKeyFilePath != null && encryptionKeyFilePath.length() > 0
            );

            // Marks the update of this universe as a success only if all the tasks before it succeeded.
            createMarkUniverseUpdateSuccessTasks()
                    .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

            // Run all the tasks.
            subTaskGroupQueue.run();
        } catch (Throwable t) {
            LOG.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
            throw t;
        } finally {
            // Mark the update of the universe as done. This will allow future edits/updates to the
            // universe to happen.
            unlockUniverseForUpdate();
        }
        LOG.info("Finished {} task.", getName());
    }
}
