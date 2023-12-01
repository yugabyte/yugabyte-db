package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.tasks.params.IProviderTaskParams;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.CloudProviderHelper;
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
@Retryable
public class CloudProviderDelete extends AbstractTaskBase {

  private AccessManager accessManager;

  private CloudProviderHelper cloudProviderHelper;

  @Inject
  protected CloudProviderDelete(
      BaseTaskDependencies baseTaskDependencies,
      AccessManager accessManager,
      CloudProviderHelper cloudProviderHelper) {
    super(baseTaskDependencies);
    this.accessManager = accessManager;
    this.cloudProviderHelper = cloudProviderHelper;
  }

  // IProviderTaskParams automatically enables locking logic in ProviderEditRestrictionManager
  public static class Params extends AbstractTaskParams implements IProviderTaskParams {
    public UUID providerUUID;
    public Customer customer;

    @Override
    public UUID getProviderUUID() {
      return providerUUID;
    }
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    UUID providerUUID = taskParams().providerUUID;
    log.info("Trying to delete provider with UUID {}", providerUUID);
    Customer customer = taskParams().customer;
    Provider provider = Provider.getOrBadRequest(customer.getUuid(), providerUUID);

    if (customer.getUniversesForProvider(provider.getUuid()).size() > 0) {
      throw new IllegalStateException("Cannot delete Provider with Universes");
    }
    provider.setUsabilityState(Provider.UsabilityState.ERROR);
    provider.save();

    // Clear the key files in the DB.
    String keyFileBasePath = accessManager.getOrCreateKeyFilePath(provider.getUuid());
    // We would delete only the files for k8s provider
    // others are already taken care off during access key deletion.
    boolean isKubernetes = provider.getCode().equals(CloudType.kubernetes.toString());
    FileData.deleteFiles(keyFileBasePath, isKubernetes);

    // Clear Access Key related metadata
    for (AccessKey accessKey : AccessKey.getAll(provider.getUuid())) {
      final String provisionInstanceScript = provider.getDetails().provisionInstanceScript;
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

    cloudProviderHelper.updatePrometheusConfig(provider);

    log.info("Finished {} task.", getName());
  }
}
