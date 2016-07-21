// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.api.forms;

import com.yugabyte.yw.api.models.Customer;

import play.data.validation.Constraints;


/**
 * This class will be used by the API and UI Form Elements to validate constraints are met
 */
public class CustomerRegisterFormData {
  @Constraints.Required()
  @Constraints.Email
  @Constraints.MinLength(5)
  public String email;

  @Constraints.Required()
  @Constraints.MinLength(6)
  public String password;

  @Constraints.Required()
  @Constraints.MinLength(3)
  public String name;

	public static CustomerRegisterFormData createFromCustomer(Customer cust) {
		CustomerRegisterFormData data = new CustomerRegisterFormData();
		data.email = cust.getEmail();
		data.name = cust.name;
		return data;
	}
}
