// Copyright 2021 YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.alerts.MaintenanceService;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.filters.MaintenanceWindowApiFilter;
import com.yugabyte.yw.forms.paging.MaintenanceWindowPagedApiQuery;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.MaintenanceWindow;
import com.yugabyte.yw.models.filters.MaintenanceWindowFilter;
import com.yugabyte.yw.models.paging.MaintenanceWindowPagedQuery;
import com.yugabyte.yw.models.paging.MaintenanceWindowPagedResponse;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.List;
import java.util.UUID;
import play.mvc.Result;

@Api(
    value = "Maintenance windows",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class MaintenanceController extends AuthenticatedController {

  private final MaintenanceService maintenanceService;

  @Inject
  public MaintenanceController(MaintenanceService maintenanceService) {
    this.maintenanceService = maintenanceService;
  }

  @ApiOperation(value = "Get details of a maintenance window", response = MaintenanceWindow.class)
  public Result get(UUID customerUUID, UUID windowUUID) {
    Customer.getOrBadRequest(customerUUID);

    MaintenanceWindow window = maintenanceService.getOrBadRequest(windowUUID);
    return PlatformResults.withData(window);
  }

  @ApiOperation(
      value = "List maintenance windows",
      response = MaintenanceWindow.class,
      responseContainer = "List",
      nickname = "listOfMaintenanceWindows")
  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    MaintenanceWindowApiFilter apiFilter = parseJsonAndValidate(MaintenanceWindowApiFilter.class);
    MaintenanceWindowFilter filter =
        apiFilter.toFilter().toBuilder().customerUuid(customerUUID).build();
    List<MaintenanceWindow> windows = maintenanceService.list(filter);
    return PlatformResults.withData(windows);
  }

  @ApiOperation(
      value = "List maintenance windows (paginated)",
      response = MaintenanceWindowPagedResponse.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PageMaintenanceWindowsRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.paging.MaintenanceWindowPagedApiQuery",
          required = true))
  public Result page(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    MaintenanceWindowPagedApiQuery apiQuery =
        parseJsonAndValidate(MaintenanceWindowPagedApiQuery.class);
    MaintenanceWindowApiFilter apiFilter = apiQuery.getFilter();
    MaintenanceWindowFilter filter =
        apiFilter.toFilter().toBuilder().customerUuid(customerUUID).build();
    MaintenanceWindowPagedQuery query =
        apiQuery.copyWithFilter(filter, MaintenanceWindowPagedQuery.class);

    MaintenanceWindowPagedResponse windows = maintenanceService.pagedList(query);

    return PlatformResults.withData(windows);
  }

  @ApiOperation(value = "Create maintenance window", response = MaintenanceWindow.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "CreateMaintenanceWindowRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.MaintenanceWindow",
          required = true))
  public Result create(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    MaintenanceWindow window = parseJson(MaintenanceWindow.class);

    if (window.getUuid() != null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't create window with uuid set");
    }

    window = maintenanceService.save(window);

    auditService().createAuditEntry(ctx(), request());
    return PlatformResults.withData(window);
  }

  @ApiOperation(value = "Update maintenance window", response = MaintenanceWindow.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "UpdateMaintenanceWindowRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.MaintenanceWindow",
          required = true))
  public Result update(UUID customerUUID, UUID windowUUID) {
    Customer.getOrBadRequest(customerUUID);
    maintenanceService.getOrBadRequest(windowUUID);

    MaintenanceWindow window = parseJson(MaintenanceWindow.class);

    if (window.getUuid() == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't update window with missing uuid");
    }

    if (!window.getUuid().equals(windowUUID)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Window UUID from path should be consistent with body");
    }

    window = maintenanceService.save(window);

    auditService().createAuditEntry(ctx(), request());
    return PlatformResults.withData(window);
  }

  @ApiOperation(value = "Delete maintenance window", response = YBPSuccess.class)
  public Result delete(UUID customerUUID, UUID windowUUID) {
    Customer.getOrBadRequest(customerUUID);

    maintenanceService.getOrBadRequest(windowUUID);

    maintenanceService.delete(windowUUID);

    auditService().createAuditEntry(ctx(), request());
    return YBPSuccess.empty();
  }
}
