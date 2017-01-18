// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;

public class ModelFactory {
  public static Customer testCustomer() {
    return Customer.create("Test customer", "test@customer.com", "password");
  }
  public static Provider awsProvider(Customer customer) {
    return Provider.create(customer.uuid, "aws", "Amazon");
  }
  public static Provider gceProvider(Customer customer) {
    return Provider.create(customer.uuid, "gce", "Google");
  }
  public static Provider newProvider(Customer customer, Common.CloudType cloud) {
    return Provider.create(customer.uuid, cloud.toString(), cloud.toString());
  }
}
