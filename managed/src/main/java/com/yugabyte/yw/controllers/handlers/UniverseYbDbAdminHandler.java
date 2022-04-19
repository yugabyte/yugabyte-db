/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YcqlQueryExecutor;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.DatabaseSecurityFormData;
import com.yugabyte.yw.forms.DatabaseUserFormData;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.Optional;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Controller;
import play.mvc.Http;

public class UniverseYbDbAdminHandler {
  @VisibleForTesting
  public static final String RUN_QUERY_ISNT_ALLOWED =
      "run_query not supported for this application";

  private static final Logger LOG = LoggerFactory.getLogger(UniverseYbDbAdminHandler.class);

  @VisibleForTesting public static final String LEARN_DOMAIN_NAME = "learn.yugabyte.com";

  @Inject play.Configuration appConfig;
  @Inject ConfigHelper configHelper;
  @Inject RuntimeConfigFactory runtimeConfigFactory;
  @Inject YsqlQueryExecutor ysqlQueryExecutor;
  @Inject YcqlQueryExecutor ycqlQueryExecutor;

  public UniverseYbDbAdminHandler() {}

  private static boolean isCorrectOrigin() {
    boolean correctOrigin = false;
    Optional<String> origin = Controller.request().header(Http.HeaderNames.ORIGIN);
    if (origin.isPresent()) {
      try {
        URI uri = new URI(origin.get());
        correctOrigin = LEARN_DOMAIN_NAME.equals(uri.getHost());
      } catch (URISyntaxException e) {
        LOG.debug("Ignored exception: " + e.getMessage());
      }
    }
    return correctOrigin;
  }

  public void setDatabaseCredentials(
      Customer customer, Universe universe, DatabaseSecurityFormData dbCreds) {
    if (!runtimeConfigFactory.forCustomer(customer).getBoolean("yb.cloud.enabled")) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Customer type.");
    }

    dbCreds.validation();

    if (!StringUtils.isEmpty(dbCreds.ysqlAdminUsername)) {
      ysqlQueryExecutor.updateAdminPassword(universe, dbCreds);
    }

    if (!StringUtils.isEmpty(dbCreds.ycqlAdminUsername)) {
      ycqlQueryExecutor.updateAdminPassword(universe, dbCreds);
    }
  }

  public void createUserInDB(Customer customer, Universe universe, DatabaseUserFormData data) {
    if (!runtimeConfigFactory.forCustomer(customer).getBoolean("yb.cloud.enabled")) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Customer type.");
    }
    data.validation();

    if (!StringUtils.isEmpty(data.ysqlAdminUsername)) {
      ysqlQueryExecutor.createUser(universe, data);
    }
    if (!StringUtils.isEmpty(data.ycqlAdminUsername)) {
      ycqlQueryExecutor.createUser(universe, data);
    }
  }

  public JsonNode validateRequestAndExecuteQuery(
      Universe universe, RunQueryFormData runQueryFormData) {
    String mode = appConfig.getString("yb.mode", "PLATFORM");
    if (!mode.equals("OSS")) {
      throw new PlatformServiceException(BAD_REQUEST, RUN_QUERY_ISNT_ALLOWED);
    }

    String securityLevel =
        (String) configHelper.getConfig(ConfigHelper.ConfigType.Security).get("level");
    if (!isCorrectOrigin() || securityLevel == null || !securityLevel.equals("insecure")) {
      throw new PlatformServiceException(BAD_REQUEST, RUN_QUERY_ISNT_ALLOWED);
    }

    return ysqlQueryExecutor.executeQuery(universe, runQueryFormData);
  }
}
