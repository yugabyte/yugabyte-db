/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;
import java.io.File;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YBClient;
import play.api.Play;

import java.util.List;

public class CopyEncryptionKeyFile extends NodeTaskBase {

    public static final Logger LOG = LoggerFactory.getLogger(CopyEncryptionKeyFile.class);

    // Timeout for failing to respond to pings.
    private static final long TIMEOUT_SERVER_WAIT_MS = 120000;

    public static class Params extends NodeTaskParams {}

    @Override
    protected Params taskParams() {
        return (Params)taskParams;
    }

    @Override
    public void run() {
        Universe universe = Universe.get(taskParams().universeUUID);
        if (taskParams().encryptionKeyFilePath != null) {
            try {
                // Execute the ansible command.
                List<ShellProcessHandler.ShellResponse> responses = getNodeManager()
                        .copyEncryptionKeyFile(taskParams());
                responses.stream().forEach(resp -> logShellResponse(resp));
            } catch (Exception e) {
                LOG.error("{} hit error : {}", getName(), e.getMessage());
                throw new RuntimeException(e);
            }
        }
    }
}
