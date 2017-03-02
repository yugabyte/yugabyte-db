// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;

public class ModelFactory {
  public static Customer testCustomer() {
    return testCustomer("test@customer.com");
  }
  public static Customer testCustomer(String email) {
    return Customer.create("Test customer", email, "password");
  }
  public static Provider awsProvider(Customer customer) {
    return Provider.create(customer.uuid, Common.CloudType.aws, "Amazon");
  }
  public static Provider gceProvider(Customer customer) {
    return Provider.create(customer.uuid, Common.CloudType.gcp, "Google");
  }
  public static Provider newProvider(Customer customer, Common.CloudType cloud) {
    return Provider.create(customer.uuid, cloud, cloud.toString());
  }
}
