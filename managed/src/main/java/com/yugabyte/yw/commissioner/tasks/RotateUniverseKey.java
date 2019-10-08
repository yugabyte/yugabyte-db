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

public class RotateUniverseKey extends UniverseDefinitionTaskBase {
    public static final Logger LOG = LoggerFactory.getLogger(RotateUniverseKey.class);

    @Override
    public void run() {
        LOG.info("Started {} task.", getName());
        try {
            String encryptionKeyFilePath = taskParams().encryptionKeyFilePath;
            // Verify the task params.
            if (encryptionKeyFilePath == null || encryptionKeyFilePath.isEmpty()) {
                throw new IllegalArgumentException("An encryption key file path has not been set");
            }

            // Create the task list sequence.
            subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

            // Update the universe DB with the update to be performed and set the
            // 'updateInProgress' flag to prevent other updates from happening.
            lockUniverseForUpdate(taskParams().expectedUniverseVersion);

            // TODO: (Daniel) - Actually copy the file over to nodes on VM provider
            //  before setting encryptionEnabled=true

            // Enable encryption-at-rest with new key file contents
            createEnableEncryptionAtRestTask(encryptionKeyFilePath)
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
