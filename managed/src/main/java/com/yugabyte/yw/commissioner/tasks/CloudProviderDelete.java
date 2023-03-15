package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.ProviderEditRestrictionManager;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.FileData;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import java.io.File;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CloudProviderDelete extends AbstractTaskBase {

  private AccessManager accessManager;
  private ProviderEditRestrictionManager providerEditRestrictionManager;

  @Inject
  protected CloudProviderDelete(
      BaseTaskDependencies baseTaskDependencies,
      AccessManager accessManager,
      ProviderEditRestrictionManager providerEditRestrictionManager) {
    super(baseTaskDependencies);
    this.accessManager = accessManager;
    this.providerEditRestrictionManager = providerEditRestrictionManager;
  }

  public static class Params extends AbstractTaskParams {
    public UUID providerUUID;
    public Customer customer;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    UUID providerUUID = taskParams().providerUUID;
    providerEditRestrictionManager.tryEditProvider(
        providerUUID,
        () -> {
          log.info("Trying to delete provider with UUID {}", providerUUID);
          Customer customer = taskParams().customer;
          Provider provider = Provider.getOrBadRequest(customer.uuid, providerUUID);

          if (customer.getUniversesForProvider(provider.uuid).size() > 0) {
            throw new IllegalStateException("Cannot delete Provider with Universes");
          }

          // Clear the key files in the DB.
          String keyFileBasePath = accessManager.getOrCreateKeyFilePath(provider.uuid);
          // We would delete only the files for k8s provider
          // others are already taken care off during access key deletion.
          FileData.deleteFiles(
              keyFileBasePath, provider.code.equals(CloudType.kubernetes.toString()));

          // Clear Access Key related metadata
          for (AccessKey accessKey : AccessKey.getAll(provider.uuid)) {
            final String provisionInstanceScript = provider.details.provisionInstanceScript;
            if (!provisionInstanceScript.isEmpty()) {
              new File(provisionInstanceScript).delete();
            }
            accessManager.deleteKeyByProvider(
                provider, accessKey.getKeyCode(), accessKey.getKeyInfo().deleteRemote);
            accessKey.delete();
          }

          // Clear Node instance for the provider.
          NodeInstance.deleteByProvider(providerUUID);
          // Delete the instance types for the provider.
          InstanceType.deleteInstanceTypesForProvider(provider, config);

          // Delete the provider.
          provider.delete();
        });
    log.info("Finished {} task.", getName());
  }
}
