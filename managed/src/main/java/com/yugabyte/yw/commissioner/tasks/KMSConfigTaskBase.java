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

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.KMSConfigTaskParams;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.forms.ITaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;

public abstract class KMSConfigTaskBase extends AbstractTaskBase {
    public static final Logger LOG = LoggerFactory.getLogger(KMSConfigTaskBase.class);

    public EncryptionAtRestManager kmsManager;

    @Override
    protected KMSConfigTaskParams taskParams() {
        return (KMSConfigTaskParams) taskParams;
    }

    @Override
    public void initialize(ITaskParams params) {
        super.initialize(params);
        kmsManager = Play.current().injector().instanceOf(EncryptionAtRestManager.class);
        // Create the threadpool for the subtasks to use.
        createThreadpool();
    }
}
