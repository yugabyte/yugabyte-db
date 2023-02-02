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
import play.api.Play;

public class CloudAccessKeySetup extends CloudTaskBase {

  private TemplateManager templateManager;

  @Inject
  protected CloudAccessKeySetup(
      BaseTaskDependencies baseTaskDependencies, TemplateManager templateManager) {
    super(baseTaskDependencies);
    this.templateManager = templateManager;
  }

  public static class Params extends CloudBootstrap.Params {
    public String regionCode;
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
    AccessManager accessManager = Play.current().injector().instanceOf(AccessManager.class);

    // TODO(bogdan): validation at higher level?
    String accessKeyCode =
        Strings.isNullOrEmpty(taskParams().keyPairName)
            ? AccessKey.getDefaultKeyCode(provider)
            : taskParams().keyPairName;

    if (!Strings.isNullOrEmpty(taskParams().sshPrivateKeyContent)) {
      accessManager.saveAndAddKey(
          region.uuid,
          taskParams().sshPrivateKeyContent,
          accessKeyCode,
          AccessManager.KeyType.PRIVATE,
          taskParams().sshUser,
          taskParams().sshPort,
          taskParams().airGapInstall,
          false,
          taskParams().setUpChrony,
          taskParams().ntpServers,
          taskParams().showSetUpChrony,
          taskParams().skipKeyPairValidate);
    } else {
      // For add region, we should verify if the skipKeyPairValidate is set, so that we don't
      // try to add the key unnecessarily. It is false by default, so unless someone explicitly
      // sets it, the key will be added.
      if (!taskParams().skipKeyPairValidate) {
        accessManager.addKey(
            region.uuid,
            accessKeyCode,
            null,
            taskParams().sshUser,
            taskParams().sshPort,
            taskParams().airGapInstall,
            false,
            taskParams().setUpChrony,
            taskParams().ntpServers,
            taskParams().showSetUpChrony);
      }
    }

    if (provider.getCloudCode().equals(Common.CloudType.onprem)) {
      // In case of onprem provider, we add a couple of additional attributes like passwordlessSudo
      // and create a pre-provision script
      AccessKey accessKey = AccessKey.getOrBadRequest(provider.uuid, accessKeyCode);
      templateManager.createProvisionTemplate(
          accessKey,
          provider.details.airGapInstall,
          provider.details.passwordlessSudoAccess,
          provider.details.installNodeExporter,
          provider.details.nodeExporterPort,
          provider.details.nodeExporterUser,
          provider.details.setUpChrony,
          provider.details.ntpServers);
    }
  }
}
