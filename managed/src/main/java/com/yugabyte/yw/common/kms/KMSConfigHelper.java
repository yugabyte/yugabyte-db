// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.kms;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.params.KMSConfigTaskParams;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Helper that encapsulates submission of KMS config create/edit/delete tasks so that both the REST
 * controller and the Kubernetes operator reconciler share a single, retryable entry point.
 */
@Slf4j
@Singleton
public class KMSConfigHelper {

  private final Commissioner commissioner;

  @Inject
  public KMSConfigHelper(Commissioner commissioner) {
    this.commissioner = commissioner;
  }

  /**
   * Submits a create KMS config task. The {@code formData} must contain the {@code name} field; it
   * is extracted into the task params and removed from the provider config before submission.
   */
  public UUID createKMSConfig(UUID customerUUID, KeyProvider keyProvider, ObjectNode formData) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    KMSConfigTaskParams taskParams = new KMSConfigTaskParams();
    taskParams.kmsProvider = keyProvider;
    taskParams.providerConfig = formData;
    taskParams.customerUUID = customerUUID;
    taskParams.kmsConfigName = formData.get("name").asText();
    formData.remove("name");
    UUID taskUUID = commissioner.submit(TaskType.CreateKMSConfig, taskParams);
    log.info("Submitted create KMS config for {}, task uuid = {}.", customerUUID, taskUUID);
    CustomerTask.create(
        customer,
        customerUUID,
        taskUUID,
        CustomerTask.TargetType.KMSConfiguration,
        CustomerTask.TaskType.Create,
        taskParams.getName());
    return taskUUID;
  }

  public UUID editKMSConfig(
      UUID customerUUID,
      UUID configUUID,
      KeyProvider keyProvider,
      String kmsConfigName,
      ObjectNode formData) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    KMSConfigTaskParams taskParams = new KMSConfigTaskParams();
    taskParams.configUUID = configUUID;
    taskParams.kmsProvider = keyProvider;
    taskParams.providerConfig = formData;
    taskParams.kmsConfigName = kmsConfigName;
    taskParams.customerUUID = customerUUID;
    formData.remove("name");
    UUID taskUUID = commissioner.submit(TaskType.EditKMSConfig, taskParams);
    log.info("Submitted edit KMS config for {}, task uuid = {}.", customerUUID, taskUUID);
    CustomerTask.create(
        customer,
        customerUUID,
        taskUUID,
        CustomerTask.TargetType.KMSConfiguration,
        CustomerTask.TaskType.Update,
        taskParams.getName());
    return taskUUID;
  }

  public UUID deleteKMSConfig(UUID customerUUID, UUID configUUID, KeyProvider keyProvider) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    KMSConfigTaskParams taskParams = new KMSConfigTaskParams();
    taskParams.kmsProvider = keyProvider;
    taskParams.customerUUID = customerUUID;
    taskParams.configUUID = configUUID;
    UUID taskUUID = commissioner.submit(TaskType.DeleteKMSConfig, taskParams);
    log.info("Submitted delete KMS config for {}, task uuid = {}.", customerUUID, taskUUID);
    CustomerTask.create(
        customer,
        customerUUID,
        taskUUID,
        CustomerTask.TargetType.KMSConfiguration,
        CustomerTask.TaskType.Delete,
        taskParams.getName());
    return taskUUID;
  }
}
