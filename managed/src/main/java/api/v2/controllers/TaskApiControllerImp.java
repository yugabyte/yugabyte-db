// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.models.TaskPagedQuerySpec;
import api.v2.models.TaskPagedResp;
import com.google.inject.Inject;
import com.yugabyte.yw.common.tasks.CustomerTaskHandler;
import java.util.UUID;
import play.mvc.Http.Request;

public class TaskApiControllerImp extends TaskApiControllerImpInterface {

  private final CustomerTaskHandler customerTaskHandler;

  @Inject
  public TaskApiControllerImp(CustomerTaskHandler customerTaskHandler) {
    this.customerTaskHandler = customerTaskHandler;
  }

  @Override
  public TaskPagedResp pageListTasks(
      Request request, UUID cUUID, TaskPagedQuerySpec taskPagedQuerySpec) throws Exception {
    return customerTaskHandler.pageListTasks(cUUID, taskPagedQuerySpec);
  }
}
