// Copyright 2020 YugaByte, Inc.
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

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Audit.ActionType;
import com.yugabyte.yw.models.Audit.TargetType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.Callable;
import java.util.function.Consumer;
import lombok.extern.slf4j.Slf4j;
import org.yb.perf_advisor.filters.PerformanceRecommendationFilter;
import org.yb.perf_advisor.filters.StateChangeAuditInfoFilter;
import org.yb.perf_advisor.models.PerformanceRecommendation;
import org.yb.perf_advisor.models.PerformanceRecommendation.RecommendationState;
import org.yb.perf_advisor.models.paging.PerformanceRecommendationPagedQuery;
import org.yb.perf_advisor.models.paging.PerformanceRecommendationPagedResponse;
import org.yb.perf_advisor.models.paging.StateChangeAuditInfoPagedQuery;
import org.yb.perf_advisor.models.paging.StateChangeAuditInfoPagedResponse;
import org.yb.perf_advisor.services.db.PerformanceRecommendationService;
import org.yb.perf_advisor.services.db.StateChangeAuditInfoService;
import org.yb.perf_advisor.services.db.ValidationException;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Performance Advisor",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class PerfAdvisorController extends AuthenticatedController {

  @Inject private PerformanceRecommendationService performanceRecommendationService;
  @Inject private StateChangeAuditInfoService stateChangeAuditInfoService;

  @ApiOperation(
      value = "Get performance recommendation details",
      response = PerformanceRecommendation.class)
  public Result get(UUID customerUUID, UUID recommendationUuid) {
    Customer.getOrBadRequest(customerUUID);

    PerformanceRecommendation recommendation =
        convertException(
            () -> performanceRecommendationService.getOrThrow(recommendationUuid),
            "Get performance recommendation with id " + recommendationUuid);

    return PlatformResults.withData(recommendation);
  }

  @ApiOperation(
      value = "List performance recommendations (paginated)",
      response = PerformanceRecommendationPagedResponse.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PagePerformanceRecommendationRequest",
          paramType = "body",
          dataType = "org.yb.perf_advisor.models.paging.PerformanceRecommendationPagedQuery",
          required = true))
  public Result page(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    PerformanceRecommendationPagedQuery inputQuery =
        parseJsonAndValidate(PerformanceRecommendationPagedQuery.class);
    PerformanceRecommendationFilter inputFilter = inputQuery.getFilter();
    PerformanceRecommendationFilter filter =
        inputFilter.toBuilder().customerId(customerUUID).build();
    PerformanceRecommendationPagedQuery query =
        inputQuery.copyWithFilter(filter, PerformanceRecommendationPagedQuery.class);

    PerformanceRecommendationPagedResponse response =
        convertException(
            () -> performanceRecommendationService.pagedList(query),
            "Get performance recommendation page " + query);

    return PlatformResults.withData(response);
  }

  @ApiOperation(value = "Hide performance recommendations", response = YBPSuccess.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "HidePerformanceRecommendationsRequest",
          paramType = "body",
          dataType = "org.yb.perf_advisor.filters.PerformanceRecommendationFilter",
          required = true))
  public Result hide(UUID customerUUID) {
    return updateRecommendations(
        customerUUID,
        recommendation -> recommendation.setRecommendationState(RecommendationState.HIDDEN));
  }

  @ApiOperation(value = "Resolve performance recommendations", response = YBPSuccess.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "ResolvePerformanceRecommendationsRequest",
          paramType = "body",
          dataType = "org.yb.perf_advisor.filters.PerformanceRecommendationFilter",
          required = true))
  public Result resolve(UUID customerUUID) {
    return updateRecommendations(
        customerUUID,
        recommendation -> recommendation.setRecommendationState(RecommendationState.RESOLVED));
  }

  @ApiOperation(value = "Delete performance recommendations", response = YBPSuccess.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "DeletePerformanceRecommendationsRequest",
          paramType = "body",
          dataType = "org.yb.perf_advisor.filters.PerformanceRecommendationFilter",
          required = true))
  public Result delete(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    PerformanceRecommendationFilter inputFilter =
        parseJsonAndValidate(PerformanceRecommendationFilter.class);
    PerformanceRecommendationFilter filter =
        inputFilter.toBuilder().customerId(customerUUID).build();

    convertException(
        () -> performanceRecommendationService.delete(filter),
        "Delete performance recommendations " + filter);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            TargetType.PerformanceRecommendation,
            null,
            Audit.ActionType.Delete,
            request().body().asJson());
    return YBPSuccess.empty();
  }

  private Result updateRecommendations(
      UUID customerUUID, Consumer<PerformanceRecommendation> updater) {
    UserWithFeatures user = (UserWithFeatures) Http.Context.current().args.get("user");
    Customer.getOrBadRequest(customerUUID);

    PerformanceRecommendationFilter inputFilter =
        parseJsonAndValidate(PerformanceRecommendationFilter.class);
    PerformanceRecommendationFilter filter =
        inputFilter.toBuilder().customerId(customerUUID).build();

    List<PerformanceRecommendation> recommendations =
        convertException(
            () -> performanceRecommendationService.list(filter),
            "List performance recommendations " + filter);

    recommendations.forEach(updater);

    convertException(
        () -> performanceRecommendationService.save(recommendations, user.getUser().uuid),
        "Save performance recommendations");

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            TargetType.PerformanceRecommendation,
            null,
            ActionType.Update,
            request().body().asJson());
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "List performance recommendations state change audit events (paginated)",
      response = StateChangeAuditInfoPagedResponse.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PageStateChangeAuditInfoRequest",
          paramType = "body",
          dataType = "org.yb.perf_advisor.models.paging.StateChangeAuditInfoPagedQuery",
          required = true))
  public Result pageAuditInfo(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    StateChangeAuditInfoPagedQuery inputQuery =
        parseJsonAndValidate(StateChangeAuditInfoPagedQuery.class);
    StateChangeAuditInfoFilter inputFilter = inputQuery.getFilter();
    StateChangeAuditInfoFilter filter = inputFilter.toBuilder().customerId(customerUUID).build();
    StateChangeAuditInfoPagedQuery query =
        inputQuery.copyWithFilter(filter, StateChangeAuditInfoPagedQuery.class);

    StateChangeAuditInfoPagedResponse response =
        convertException(
            () -> stateChangeAuditInfoService.pagedList(query),
            "Get performance recommendation page " + query);

    return PlatformResults.withData(response);
  }

  private <T> T convertException(Callable<T> operation, String operationName) {
    try {
      return operation.call();
    } catch (ValidationException e) {
      JsonNode errJson = Json.toJson(e.getValidationErrors());
      throw new PlatformServiceException(BAD_REQUEST, errJson);
    } catch (Exception e) {
      log.error(operationName + " failed", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }
}
