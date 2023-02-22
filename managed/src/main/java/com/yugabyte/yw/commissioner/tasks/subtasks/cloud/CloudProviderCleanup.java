/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks.cloud;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.Provider;
import java.util.List;
import javax.inject.Inject;

public class CloudProviderCleanup extends CloudTaskBase {
  @Inject
  protected CloudProviderCleanup(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    Provider provider = Provider.get(taskParams().providerUUID);
    // Delete price components
    List<PriceComponent> priceComponents = PriceComponent.findByProvider(provider);
    if (priceComponents != null) {
      priceComponents.forEach(priceComponent -> priceComponent.delete());
    }
    // We would delete the provider, keys etc only when all the regions are cleaned up.
    if (getProvider().getRegions().isEmpty()) {
      List<AccessKey> accessKeyList = AccessKey.getAll(provider.getUuid());
      accessKeyList.forEach(accessKey -> accessKey.delete());
      provider.delete();
    }
  }
}
