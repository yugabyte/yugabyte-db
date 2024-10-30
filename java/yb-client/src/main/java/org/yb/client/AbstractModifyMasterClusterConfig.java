// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.client;

import java.util.Random;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.annotations.InterfaceAudience;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterTypes.MasterErrorPB.Code;


@InterfaceAudience.Public
public abstract class AbstractModifyMasterClusterConfig {
  private static final Logger LOG = LoggerFactory.getLogger(
      AbstractModifyMasterClusterConfig.class);
  private YBClient ybClient = null;

  public AbstractModifyMasterClusterConfig(YBClient client) {
    ybClient = client;
  }

  private CatalogEntityInfo.SysClusterConfigEntryPB getConfig() throws Exception {
    GetMasterClusterConfigResponse getResponse = ybClient.getMasterClusterConfig();
    if (getResponse.hasError()) {
      throw new RuntimeException("Get config hit error: " + getResponse.errorMessage());
    }

    return getResponse.getConfig();
  }

  public CatalogEntityInfo.SysClusterConfigEntryPB doCall() throws Exception {
    // Generate an initial random back off between 100 and 200 Ms.
    Random rand = new Random();
    long sleepTimeMs = 100 + rand.nextInt(100);

    for (int i = 0; i < 3; i++){
      CatalogEntityInfo.SysClusterConfigEntryPB newConfig = modifyConfig(getConfig());
      String configMismatchError = "";
      try {
        ChangeMasterClusterConfigResponse changeResp =
            ybClient.changeMasterClusterConfig(newConfig);
        // Generally, ybClient will raise an exception instead of returning the changeResp with
        // and error. But, to ensure there are no cases where changeResp has an error, we will also
        // validate it for retry possibility.
        if (!changeResp.hasError()) {
          return getConfig();
        }
        // Retry on 'config version mismatch' with an increasing backoff between retries.
        // All other errors should exit immediately.
        if (changeResp.errorCode() != Code.CONFIG_VERSION_MISMATCH){
          throw new RuntimeException("ChangeConfig hit error: " + changeResp.errorMessage());
        }
        configMismatchError = changeResp.errorMessage();
      } catch (MasterErrorException e) {
        // Rethrow the exception if it is not a CONFIG_VERSION_MISMATCH error.
        if (e.error == null || e.error.getCode() != Code.CONFIG_VERSION_MISMATCH) {
          throw e;
        }
        configMismatchError = e.getMessage();
      }
      LOG.warn("Got CONFIG_VERSION_MISMATCH error, retrying after {} ms: {}",
          sleepTimeMs, configMismatchError);
      Thread.sleep(sleepTimeMs);
      sleepTimeMs = sleepTimeMs * 2;
    }
    throw new RuntimeException("Failed to update config after retries: config version mismatch");
  }

  abstract protected CatalogEntityInfo.SysClusterConfigEntryPB modifyConfig(
    CatalogEntityInfo.SysClusterConfigEntryPB config);
}
