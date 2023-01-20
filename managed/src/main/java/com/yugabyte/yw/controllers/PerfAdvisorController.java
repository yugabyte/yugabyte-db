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
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.ConfKeyInfo;
import com.yugabyte.yw.common.config.RuntimeConfService;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.PerfAdvisorSettingsFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Audit.ActionType;
import com.yugabyte.yw.models.Audit.TargetType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import io.ebean.annotation.Transactional;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.Callable;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.stream.Collectors;
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

  private static final List<ConfKeyInfo<?>> PERF_ADVISOR_RUNTIME_CONFIG_KEYS =
      ImmutableList.<ConfKeyInfo<?>>builder()
          .add(UniverseConfKeys.perfAdvisorEnabled)
          .add(UniverseConfKeys.perfAdvisorUniverseFrequencyMins)
          .add(UniverseConfKeys.perfAdvisorConnectionSkewThreshold)
          .add(UniverseConfKeys.perfAdvisorConnectionSkewMinConnections)
          .add(UniverseConfKeys.perfAdvisorConnectionSkewIntervalMins)
          .add(UniverseConfKeys.perfAdvisorCpuSkewThreshold)
          .add(UniverseConfKeys.perfAdvisorCpuSkewMinUsage)
          .add(UniverseConfKeys.perfAdvisorCpuSkewIntervalMins)
          .add(UniverseConfKeys.perfAdvisorCpuUsageThreshold)
          .add(UniverseConfKeys.perfAdvisorCpuUsageIntervalMins)
          .add(UniverseConfKeys.perfAdvisorQuerySkewThreshold)
          .add(UniverseConfKeys.perfAdvisorQuerySkewMinQueries)
          .add(UniverseConfKeys.perfAdvisorQuerySkewIntervalMins)
          .add(UniverseConfKeys.perfAdvisorRejectedConnThreshold)
          .add(UniverseConfKeys.perfAdvisorRejectedConnIntervalMins)
          .build();

  @Inject private PerformanceRecommendationService performanceRecommendationService;
  @Inject private StateChangeAuditInfoService stateChangeAuditInfoService;
  @Inject private RuntimeConfService runtimeConfService;

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

  @ApiOperation(
      value = "Get universe performance advisor settings",
      response = PerfAdvisorSettingsFormData.class)
  public Result getSettings(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    if (!customer.getCustomerId().equals(universe.customerId)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Universe " + universeUUID + " does not belong to customer " + customerUUID);
    }
    List<String> confKeys =
        PERF_ADVISOR_RUNTIME_CONFIG_KEYS
            .stream()
            .map(ConfKeyInfo::getKey)
            .collect(Collectors.toList());
    Map<String, RuntimeConfigEntry> currentValues =
        RuntimeConfigEntry.get(universeUUID, confKeys)
            .stream()
            .collect(Collectors.toMap(RuntimeConfigEntry::getPath, Function.identity()));
    PerfAdvisorSettingsFormData response = new PerfAdvisorSettingsFormData();
    response.setEnabled(getRuntimeConfigValue(currentValues, UniverseConfKeys.perfAdvisorEnabled));
    response.setRunFrequencyMins(
        getRuntimeConfigValue(currentValues, UniverseConfKeys.perfAdvisorUniverseFrequencyMins));

    response.setConnectionSkewThreshold(
        getRuntimeConfigValue(currentValues, UniverseConfKeys.perfAdvisorConnectionSkewThreshold));
    response.setConnectionSkewMinConnections(
        getRuntimeConfigValue(
            currentValues, UniverseConfKeys.perfAdvisorConnectionSkewMinConnections));
    response.setConnectionSkewIntervalMins(
        getRuntimeConfigValue(
            currentValues, UniverseConfKeys.perfAdvisorConnectionSkewIntervalMins));

    response.setCpuSkewThreshold(
        getRuntimeConfigValue(currentValues, UniverseConfKeys.perfAdvisorCpuSkewThreshold));
    response.setCpuSkewMinUsage(
        getRuntimeConfigValue(currentValues, UniverseConfKeys.perfAdvisorCpuSkewMinUsage));
    response.setCpuSkewIntervalMins(
        getRuntimeConfigValue(currentValues, UniverseConfKeys.perfAdvisorCpuSkewIntervalMins));

    response.setCpuUsageThreshold(
        getRuntimeConfigValue(currentValues, UniverseConfKeys.perfAdvisorCpuUsageThreshold));
    response.setCpuUsageIntervalMins(
        getRuntimeConfigValue(currentValues, UniverseConfKeys.perfAdvisorCpuUsageIntervalMins));

    response.setQuerySkewThreshold(
        getRuntimeConfigValue(currentValues, UniverseConfKeys.perfAdvisorQuerySkewThreshold));
    response.setQuerySkewMinQueries(
        getRuntimeConfigValue(currentValues, UniverseConfKeys.perfAdvisorQuerySkewMinQueries));
    response.setQuerySkewIntervalMins(
        getRuntimeConfigValue(currentValues, UniverseConfKeys.perfAdvisorQuerySkewIntervalMins));

    response.setRejectedConnThreshold(
        getRuntimeConfigValue(currentValues, UniverseConfKeys.perfAdvisorRejectedConnThreshold));
    response.setRejectedConnIntervalMins(
        getRuntimeConfigValue(currentValues, UniverseConfKeys.perfAdvisorRejectedConnIntervalMins));
    return PlatformResults.withData(response);
  }

  @ApiOperation(value = "Update universe performance advisor settings", response = YBPSuccess.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PerformanceAdvisorSettingsRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.PerfAdvisorSettingsFormData",
          required = true))
  @Transactional
  public Result updateSettings(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    if (!customer.getCustomerId().equals(universe.customerId)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Universe " + universeUUID + " does not belong to customer " + customerUUID);
    }
    PerfAdvisorSettingsFormData settings = parseJsonAndValidate(PerfAdvisorSettingsFormData.class);
    setRuntimeConfigValue(
        customerUUID, universeUUID, UniverseConfKeys.perfAdvisorEnabled, settings.getEnabled());
    setRuntimeConfigValue(
        customerUUID,
        universeUUID,
        UniverseConfKeys.perfAdvisorUniverseFrequencyMins,
        settings.getRunFrequencyMins());

    setRuntimeConfigValue(
        customerUUID,
        universeUUID,
        UniverseConfKeys.perfAdvisorConnectionSkewThreshold,
        settings.getConnectionSkewThreshold());
    setRuntimeConfigValue(
        customerUUID,
        universeUUID,
        UniverseConfKeys.perfAdvisorConnectionSkewMinConnections,
        settings.getConnectionSkewMinConnections());
    setRuntimeConfigValue(
        customerUUID,
        universeUUID,
        UniverseConfKeys.perfAdvisorConnectionSkewIntervalMins,
        settings.getConnectionSkewIntervalMins());

    setRuntimeConfigValue(
        customerUUID,
        universeUUID,
        UniverseConfKeys.perfAdvisorCpuSkewThreshold,
        settings.getCpuSkewThreshold());
    setRuntimeConfigValue(
        customerUUID,
        universeUUID,
        UniverseConfKeys.perfAdvisorCpuSkewMinUsage,
        settings.getCpuSkewMinUsage());
    setRuntimeConfigValue(
        customerUUID,
        universeUUID,
        UniverseConfKeys.perfAdvisorCpuSkewIntervalMins,
        settings.getCpuSkewIntervalMins());

    setRuntimeConfigValue(
        customerUUID,
        universeUUID,
        UniverseConfKeys.perfAdvisorCpuUsageThreshold,
        settings.getCpuUsageThreshold());
    setRuntimeConfigValue(
        customerUUID,
        universeUUID,
        UniverseConfKeys.perfAdvisorCpuUsageIntervalMins,
        settings.getCpuUsageIntervalMins());

    setRuntimeConfigValue(
        customerUUID,
        universeUUID,
        UniverseConfKeys.perfAdvisorQuerySkewThreshold,
        settings.getQuerySkewThreshold());
    setRuntimeConfigValue(
        customerUUID,
        universeUUID,
        UniverseConfKeys.perfAdvisorQuerySkewMinQueries,
        settings.getQuerySkewMinQueries());
    setRuntimeConfigValue(
        customerUUID,
        universeUUID,
        UniverseConfKeys.perfAdvisorQuerySkewIntervalMins,
        settings.getQuerySkewIntervalMins());

    setRuntimeConfigValue(
        customerUUID,
        universeUUID,
        UniverseConfKeys.perfAdvisorRejectedConnThreshold,
        settings.getRejectedConnThreshold());
    setRuntimeConfigValue(
        customerUUID,
        universeUUID,
        UniverseConfKeys.perfAdvisorRejectedConnIntervalMins,
        settings.getRejectedConnIntervalMins());

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            TargetType.PerformanceAdvisorSettings,
            null,
            ActionType.Update,
            request().body().asJson());
    return YBPSuccess.empty();
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

  private <T> T getRuntimeConfigValue(
      Map<String, RuntimeConfigEntry> currentValues, ConfKeyInfo<T> keyInfo) {
    RuntimeConfigEntry entry = currentValues.get(keyInfo.getKey());
    if (entry == null) {
      return null;
    }
    return keyInfo.getDataType().getParser().apply(entry.getValue());
  }

  private <T> void setRuntimeConfigValue(
      UUID customerUuid, UUID universeUuid, ConfKeyInfo<T> keyInfo, T value) {
    if (value == null) {
      runtimeConfService.deleteKeyIfPresent(customerUuid, universeUuid, keyInfo.getKey());
    } else {
      runtimeConfService.setKey(customerUuid, universeUuid, keyInfo.getKey(), value.toString());
    }
  }
}
