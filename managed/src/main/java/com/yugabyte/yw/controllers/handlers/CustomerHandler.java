/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.CloudProviderDelete;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.FileData;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Users;
import java.io.File;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CustomerHandler {

  @Inject private CloudProviderDelete cloudProviderDelete;
  @Inject private MetricService metricService;

  private void deleteRelevantFilesForCustomer(UUID customerUUID, String child) {
    log.debug("Deleting {} of customer: {}", child, customerUUID);
    File basePath = new File(AppConfigHelper.getStoragePath(), child);
    if (basePath.exists()) {
      File filePath = new File(basePath.getAbsoluteFile(), customerUUID.toString());
      if (filePath.exists() && filePath.isDirectory()) {
        FileData.deleteFiles(filePath.getAbsolutePath(), true);
      }
    }
  }

  /**
   * Handler that performs the actual deletion of the customer , related entities and files
   *
   * @param customerUUID
   */
  public void deleteCustomer(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    /*
     * Clear the customer-related files on the disk and DB
     * 1. certs, licenses, node-agent/certs on the disk
     * 2. certificate_info, customer_license, node_agent, provider tables on the DB
     * [taken care by the fk constraints related to customerUUID]
     * 3. If provider(s) are not yet deleted for the customerUUID
     * (i) Clear the provider-related files on the disk : keys, provision_scripts
     * (ii) Clear the table entries on the DB : node_instance; access_key, instance_type
     * [are cleared due to fk constraints related to provider, customer]
     */

    List<Provider> providerList = Provider.getAll(customerUUID);
    if (!providerList.isEmpty()) {
      // Provider(s) are not yet deleted for the customerUUID, so clean up the files on the disk. DB
      // cleanup is taken care by the fk constraints.
      for (Provider provider : providerList) {
        if (customer.getUniversesForProvider(provider.getUuid()).size() > 0) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              String.format("Cannot delete Provider(%s) with Universe(s)", provider.getUuid()));
        }
        cloudProviderDelete.deleteRelevantFilesForProvider(customerUUID, provider.getUuid());
        log.info("Deleted metadata of the provider {}.", provider.getUuid());
      }
    }

    // Clearing the files on the disk
    deleteRelevantFilesForCustomer(customerUUID, "/certs");
    deleteRelevantFilesForCustomer(customerUUID, "/licenses");
    deleteRelevantFilesForCustomer(customerUUID, "/node-agent/certs");

    List<Users> users = Users.getAll(customerUUID);
    for (Users user : users) {
      user.delete();
    }

    // delete the taskInfo corresponding to the customer_task
    // TODO: Alter task_info table to have foreign key reference to customer id.
    List<CustomerTask> customerTasks = CustomerTask.getByCustomerUUID(customerUUID);
    if (!customerTasks.isEmpty()) {
      for (CustomerTask task : customerTasks) {
        UUID taskUUID = task.getTaskUUID();
        TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
        if (!taskInfo.delete()) {
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR, "Unable to delete taskInfo of taskUUID: " + taskUUID);
        }
      }
    }

    if (!customer.delete()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete Customer UUID: " + customerUUID);
    }

    metricService.markSourceRemoved(customerUUID, null);
  }
}
