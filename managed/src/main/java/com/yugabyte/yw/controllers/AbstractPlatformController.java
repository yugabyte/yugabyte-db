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
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.models.Users;
import play.mvc.Controller;
import play.mvc.Http;
import play.mvc.With;

/**
 *  This class contains dependencies, which can be used by most of the Platform controllers.
 *  An example of such a functionality is the request audit.
 */
public abstract class AbstractPlatformController extends Controller {

  @Inject
  private AuditService auditService;

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
