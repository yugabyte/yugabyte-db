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

import com.google.inject.Inject;
import com.yugabyte.yw.controllers.handlers.HashedTimestampColumnFinder;
import com.yugabyte.yw.controllers.handlers.UniversePerfHandler;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.QueryDistributionSuggestionResponse;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import play.mvc.Result;

import java.util.List;
import java.util.UUID;

import static com.yugabyte.yw.common.TableSpaceStructures.HashedTimestampColumnFinderResponse;

@Api(
    value = "Universe performance suggestions",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class UniversePerfController extends AuthenticatedController {

  private HashedTimestampColumnFinder hashedTimestampColumnFinder;
  private UniversePerfHandler universePerfHandler;

  @Inject
  public UniversePerfController(
      UniversePerfHandler universePerfHandler,
      HashedTimestampColumnFinder hashedTimestampColumnFinder) {
    this.universePerfHandler = universePerfHandler;
    this.hashedTimestampColumnFinder = hashedTimestampColumnFinder;
  }

  @ApiOperation(
      value = "Get query distribution improvement suggestion for a universe",
      nickname = "getQueryDistributionSuggestions",
      response = QueryDistributionSuggestionResponse.class)
  public Result getQueryDistributionSuggestions(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
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
      value = "Return list of hash indexes",
      notes = "Returns list of hash indexes on timestamp columns.",
      nickname = "getRangeHash",
      response = HashedTimestampColumnFinderResponse.class)
  public Result getRangeHash(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    List<HashedTimestampColumnFinderResponse> result =
        hashedTimestampColumnFinder.getHashedTimestampColumns(universe);

    return PlatformResults.withData(result);
  }
}
