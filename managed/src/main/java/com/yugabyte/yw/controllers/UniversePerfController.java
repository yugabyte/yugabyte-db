/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.TableSpaceStructures.HashedTimestampColumnFinderResponse;
import static com.yugabyte.yw.common.TableSpaceStructures.UnusedIndexFinderResponse;

import com.google.inject.Inject;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.HashedTimestampColumnFinder;
import com.yugabyte.yw.controllers.handlers.UniversePerfHandler;
import com.yugabyte.yw.controllers.handlers.UnusedIndexFinder;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.QueryDistributionSuggestionResponse;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.List;
import java.util.UUID;
import play.mvc.Result;

@Api(
    value = "Universe performance suggestions",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class UniversePerfController extends AuthenticatedController {

  private HashedTimestampColumnFinder hashedTimestampColumnFinder;
  private UniversePerfHandler universePerfHandler;
  private UnusedIndexFinder unusedIndexFinder;

  @Inject
  public UniversePerfController(
      UniversePerfHandler universePerfHandler,
      HashedTimestampColumnFinder hashedTimestampColumnFinder,
      UnusedIndexFinder unusedIndexFinder) {
    this.universePerfHandler = universePerfHandler;
    this.unusedIndexFinder = unusedIndexFinder;
    this.hashedTimestampColumnFinder = hashedTimestampColumnFinder;
  }

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      value = "Get query distribution improvement suggestion for a universe",
      nickname = "getQueryDistributionSuggestions",
      response = QueryDistributionSuggestionResponse.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.16.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result getQueryDistributionSuggestions(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    return PlatformResults.withData(
        universePerfHandler.universeQueryDistributionSuggestion(universe));
  }

  /**
   * API that returns the list of hash indexes on timestamp columns.
   *
   * @param customerUUID ID of customer
   * @param universeUUID ID of universe
   * @return list of serialized HashedTimestampColumnFinderResponse entries.
   */
  @ApiOperation(
      notes =
          "WARNING: This is a preview API that could change. Returns list of hash indexes on"
              + " timestamp columns.",
      value = "Return list of hash indexes",
      nickname = "getRangeHash",
      response = HashedTimestampColumnFinderResponse.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.16.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result getRangeHash(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    List<HashedTimestampColumnFinderResponse> result =
        hashedTimestampColumnFinder.getHashedTimestampColumns(universe);

    return PlatformResults.withData(result);
  }

  /**
   * API that returns the list of unused indexes in the universe.
   *
   * @param customerUUID ID of customer
   * @param universeUUID ID of universe
   * @return list of serialized UnusedIndexFinderResponse entries.
   */
  @ApiOperation(
      notes =
          "WARNING: This is a preview API that could change. "
              + "Returns list of unused indexes, along with their database and table.",
      value = "Return list of each unused index across the universe",
      nickname = "getUnusedIndexes",
      response = UnusedIndexFinderResponse.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.16.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result getUnusedIndexes(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    List<UnusedIndexFinderResponse> result = unusedIndexFinder.getUniverseUnusedIndexes(universe);

    // Returning empty json if result is empty.
    return PlatformResults.withData(result);
  }
}
