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

import com.google.common.base.Strings;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.TemplateManager;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CloudAccessKeySetup extends CloudTaskBase {

  private final TemplateManager templateManager;

  private final AccessManager accessManager;

  @Inject
  protected CloudAccessKeySetup(
      BaseTaskDependencies baseTaskDependencies,
      TemplateManager templateManager,
      AccessManager accessManager) {
    super(baseTaskDependencies);
    this.templateManager = templateManager;
    this.accessManager = accessManager;
  }

  public static class Params extends CloudBootstrap.Params {
    public String regionCode;
    public boolean isFirstTry = true;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    String regionCode = taskParams().regionCode;
    Provider provider = getProvider();
    Region region = Region.getByCode(provider, regionCode);
    if (region == null) {
      throw new RuntimeException("Region " + regionCode + " not setup.");
    }
    String accessKeyCode =
        Strings.isNullOrEmpty(taskParams().keyPairName)
            ? AccessKey.getDefaultKeyCode(provider)
            : taskParams().keyPairName;

    boolean createAccessKeyRetry = false;
    if (!taskParams().isFirstTry) {
      try {
        log.debug(
            "Trying to retrieve accessKey {} in provider {}", accessKeyCode, provider.getName());
        AccessKey accessKey = AccessKey.getOrBadRequest(provider.getUuid(), accessKeyCode);
      } catch (Exception e) {
        log.debug("AccessKey {} in provider does not exist. Creating one...", accessKeyCode);
        createAccessKeyRetry = true;
      }
    }

    if (taskParams().isFirstTry || createAccessKeyRetry) {
      if (!Strings.isNullOrEmpty(taskParams().sshPrivateKeyContent)) {
        accessManager.saveAndAddKey(
            region.getUuid(),
            taskParams().sshPrivateKeyContent,
            accessKeyCode,
            AccessManager.KeyType.PRIVATE,
            taskParams().sshUser,
            taskParams().sshPort,
            taskParams().airGapInstall,
            taskParams().skipProvisioning,
            taskParams().setUpChrony,
            taskParams().ntpServers,
            taskParams().showSetUpChrony,
            taskParams().skipKeyValidateAndUpload);
      } else {
        accessManager.addKey(
            region.getUuid(),
            accessKeyCode,
            null,
            taskParams().sshUser,
            taskParams().sshPort,
            taskParams().airGapInstall,
            taskParams().skipProvisioning,
            taskParams().setUpChrony,
            taskParams().ntpServers,
            taskParams().showSetUpChrony);
      }
    }

    if (provider.getCloudCode().equals(Common.CloudType.onprem)) {
      // In case of onprem provider, we add a couple of additional attributes like passwordlessSudo
      // and create a pre-provision script
      AccessKey accessKey = AccessKey.getOrBadRequest(provider.getUuid(), accessKeyCode);
      templateManager.createProvisionTemplate(
          accessKey,
          provider.getDetails().airGapInstall,
          provider.getDetails().passwordlessSudoAccess,
          provider.getDetails().installNodeExporter,
          provider.getDetails().nodeExporterPort,
          provider.getDetails().nodeExporterUser,
          provider.getDetails().setUpChrony,
          provider.getDetails().ntpServers);
    }
  }
}
