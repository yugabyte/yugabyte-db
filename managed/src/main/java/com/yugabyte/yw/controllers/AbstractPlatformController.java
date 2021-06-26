/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.controllers;

import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.models.Users;
import io.swagger.annotations.*;
import play.mvc.Controller;
import play.mvc.Http;

import static com.yugabyte.yw.controllers.TokenAuthenticator.API_TOKEN_HEADER;

/**
 * This class contains dependencies, which can be used by most of the Platform controllers. An
 * example of such a functionality is the request audit.
 */
@SwaggerDefinition(
    info =
        @Info(
            title = "Yugabyte Platform API",
            description = "Yugabyte Platform API",
            version = "v1",
            contact = @Contact(name = "Yugabyte", url = "http://docs.yugabyte.com"),
            license =
                @License(
                    name = AbstractPlatformController.LICENSE_1_0_0_NAME,
                    url = AbstractPlatformController.POLYFORM_FREE_TRIAL_LICENSE_1_0_0_URL)),
    consumes = {"application/json"},
    produces = {"application/json"},
    schemes = {SwaggerDefinition.Scheme.HTTP, SwaggerDefinition.Scheme.HTTPS},
    externalDocs =
        @ExternalDocs(value = "About Yugabyte Platform", url = "https://docs.yugabyte.com"),
    securityDefinition =
        @SecurityDefinition(
            apiKeyAuthDefinitions = {
              @ApiKeyAuthDefinition(
                  key = AbstractPlatformController.API_KEY_AUTH,
                  name = API_TOKEN_HEADER,
                  in = ApiKeyAuthDefinition.ApiKeyLocation.HEADER,
                  description = "Api key passed as header")
            }))
public abstract class AbstractPlatformController extends Controller {

  protected static final String LICENSE_1_0_0_NAME = "Polyform Free Trial License 1.0.0";
  protected static final String POLYFORM_FREE_TRIAL_LICENSE_1_0_0_URL =
      "https://github.com/yugabyte/yugabyte-db/blob/master/licenses/"
          + "POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt";
  protected static final String API_KEY_AUTH = "apiKeyAuth";

  @Inject protected ValidatingFormFactory formFactory;

  @Inject private AuditService auditService;

  protected AuditService auditService() {
    Users user = (Users) Http.Context.current().args.get("user");
    if (user == null) {
      throw new IllegalStateException("Shouldn't audit unauthenticated requests!");
    }
    return auditService;
  }

  @VisibleForTesting
  public void setAuditService(AuditService auditService) {
    this.auditService = auditService;
  }
}
