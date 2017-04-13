// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.cloud;

import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.List;

public class CloudProviderCleanup extends CloudTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CloudProviderCleanup.class);

  @Override
  public void run() {
    Provider provider = Provider.get(taskParams().providerUUID);
    // We would delete the provider, keys etc only when all the regions are cleaned up.
    if (getProvider().regions.isEmpty()) {
      List<AccessKey> accessKeyList = AccessKey.getAll(provider.uuid);
      accessKeyList.forEach(accessKey -> accessKey.delete());
      provider.delete();
    }
  }
}
