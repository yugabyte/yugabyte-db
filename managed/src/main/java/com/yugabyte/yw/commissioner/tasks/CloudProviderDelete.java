package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.tasks.params.IProviderTaskParams;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.CloudProviderHelper;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.FileData;
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

  public void deleteRelevantFilesForProvider(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);

    // Clear the key files in the DB.
    String keyFileBasePath = accessManager.getOrCreateKeyFilePath(provider.getUuid());
    FileData.deleteFiles(keyFileBasePath, true);

    final String provisionInstanceScript = provider.getDetails().provisionInstanceScript;
    if (!provisionInstanceScript.isEmpty()) {
      new File(provisionInstanceScript).delete();
    }

    for (AccessKey accessKey : AccessKey.getAll(provider.getUuid())) {
      accessManager.deleteKeyByProvider(provider, accessKey);
    }
    // Clear Node instance for the provider.
    NodeInstance.deleteByProvider(providerUUID);
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

    provider.setUsabilityState(Provider.UsabilityState.DELETING);
    provider.save();

    try {
      deleteRelevantFilesForProvider(customer.getUuid(), providerUUID);

      // Delete the provider.
      provider.delete();

      cloudProviderHelper.updatePrometheusConfig(provider);

      log.info("Finished {} task.", getName());
    } catch (Exception e) {
      // Handle errors, set provider state back to ERROR, and log the error
      provider.setUsabilityState(Provider.UsabilityState.ERROR);
      provider.save();
      log.error("An error occurred during provider deletion: {}", e.getMessage(), e);
      throw e;
    }
  }
}
