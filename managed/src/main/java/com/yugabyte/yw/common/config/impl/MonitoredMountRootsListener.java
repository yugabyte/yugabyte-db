/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.config.impl;

import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfigChangeListener;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import io.ebean.DB;
import java.util.UUID;
import javax.inject.Singleton;

@Singleton
public class MonitoredMountRootsListener implements RuntimeConfigChangeListener {

  public String getKeyPath() {
    return ProviderConfKeys.monitoredMountRoots.getKey();
  }

  @Override
  public void processGlobal() {
    DB.getDefault()
        .sqlUpdate(
            "update alert_definition set config_written = false where configuration_uuid in"
                + " (select uuid from alert_configuration "
                + "where template = 'NODE_SYSTEM_DISK_USAGE');")
        .execute();
  }

  @Override
  public void processCustomer(Customer customer) {
    regenerateRulesForCustomer(customer.getUuid());
  }

  @Override
  public void processProvider(Provider provider) {
    regenerateRulesForCustomer(provider.getCustomerUUID());
  }

  private void regenerateRulesForCustomer(UUID customerUuid) {
    DB.getDefault()
        .execute(
            DB.getDefault()
                .createCallableSql(
                    "update alert_definition set config_written = false where configuration_uuid in"
                        + " (select uuid from alert_configuration where customer_uuid = ?"
                        + " and template = 'NODE_SYSTEM_DISK_USAGE');")
                .setParameter(1, customerUuid));
  }
}
