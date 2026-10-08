/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.user;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import java.io.IOException;
import java.io.InputStream;
import javax.inject.Inject;
import javax.inject.Singleton;
import play.Environment;

@Singleton
public class UserService {

  private final Environment environment;

  private final RoleBindingUtil roleBindingUtil;

  @Inject
  public UserService(Environment environment, RoleBindingUtil roleBindingUtil) {
    this.environment = environment;
    this.roleBindingUtil = roleBindingUtil;
  }

  public UserWithFeatures getUserWithFeatures(Customer customer, Users user) {
    try {
      UserWithFeatures userWithFeatures = new UserWithFeatures().setUser(user);
      // A SuperAdmin granted through a role binding still reads as ConnectOnly in users.role, so
      // keying the feature set off that column alone would hand them a stripped-down UI.
      Users.Role effectiveRole =
          roleBindingUtil.isSuperAdmin(user) ? Users.Role.SuperAdmin : user.getRole();
      if (effectiveRole == null) {
        return userWithFeatures;
      }
      String configFile = effectiveRole.getFeaturesFile();
      if (configFile == null) {
        return userWithFeatures;
      }
      try (InputStream featureStream = environment.resourceAsStream(configFile)) {
        ObjectMapper mapper = new ObjectMapper();
        JsonNode features = mapper.readTree(featureStream);
        return userWithFeatures.setFeatures(features);
      }
    } catch (IOException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to parse user features config file: " + e.getMessage());
    }
  }
}
