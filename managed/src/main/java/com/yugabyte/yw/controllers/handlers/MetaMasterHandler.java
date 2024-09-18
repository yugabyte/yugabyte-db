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

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.controllers.apiModels.MasterLBStateResponse;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.Universe;
import java.time.Duration;
import java.util.ArrayList;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.GetLoadBalancerStateResponse;
import org.yb.client.IsLoadBalancerIdleResponse;
import org.yb.client.MasterErrorException;
import org.yb.client.YBClient;
import org.yb.master.MasterTypes.MasterErrorPB;

@Slf4j
public class MetaMasterHandler {

  @Inject Commissioner commissioner;
  @Inject private YBClientService ybService;
  @Inject private MetricQueryHelper metricQueryHelper;
  @Inject private RuntimeConfigFactory runtimeConfigFactory;

  public MasterLBStateResponse getMasterLBState(UUID customerUUID, UUID universeUUID) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    String masterAddresses = universe.getMasterAddresses();
    String universeCertificate = universe.getCertificateNodetoNode();
    MasterLBStateResponse resp = new MasterLBStateResponse();

    try (YBClient client = ybService.getClient(masterAddresses, universeCertificate)) {

      // Check if the tablet load balancer is actually enabled
      GetLoadBalancerStateResponse masterLBState = client.getLoadBalancerState();
      if (masterLBState == null || masterLBState.hasError() || !masterLBState.hasIsEnabled()) {
        throw new RuntimeException(
            masterLBState != null ? masterLBState.errorMessage() : "Null response");
      }
      resp.isEnabled = new Boolean(masterLBState.isEnabled());
      if (!resp.isEnabled) {
        // If it is not enabled, no point getting the current state of the tablet LB
        return resp;
      }

      try {
        IsLoadBalancerIdleResponse isBalancedResp = client.getIsLoadBalancerIdle();
        if (isBalancedResp.hasError()
            && MasterErrorPB.Code.LOAD_BALANCER_RECENTLY_ACTIVE
                != isBalancedResp.getError().getCode()) {
          // other error codes are real errors talking to the master
          throw new RuntimeException(isBalancedResp.errorMessage());
        }
        resp.isIdle = new Boolean(!isBalancedResp.hasError());
      } catch (MasterErrorException mex) {
        if (mex.error != null
            && mex.error.getCode() == MasterErrorPB.Code.LOAD_BALANCER_RECENTLY_ACTIVE) {
          resp.isIdle = new Boolean(false);
        } else {
          // other error codes are real errors talking to the master
          throw mex;
        }
      }

    } catch (Exception ex) {
      throw new PlatformServiceException(
          play.mvc.Http.Status.SERVICE_UNAVAILABLE,
          "Error reaching masters. Details: " + ex.getMessage());
    }

    try {
      if (resp.isIdle != null && !resp.isIdle) {
        resp.estTimeToBalanceSecs =
            getEstTimeToBalance(universeUUID, this.metricQueryHelper, this.runtimeConfigFactory)
                .toSeconds();
      }
    } catch (Exception ex) {
      log.trace("Unable to get an estimate of the time to balance tablet load", ex); // todo: trace
    }

    return resp;
  }

  private static Duration getEstTimeToBalance(
      UUID univUuid,
      MetricQueryHelper metricQueryHelper,
      RuntimeConfigFactory runtimeConfigFactory) {

    long scrapeIntervalSecs =
        SwamperHelper.getScrapeIntervalSeconds(runtimeConfigFactory.staticApplicationConf());
    // Query over at least 5 scrape intervals or 2 minutes
    long windowDurationSecs = Long.max(scrapeIntervalSecs * 5, Duration.ofMinutes(2).toSeconds());
    final String promFilters =
        String.format("export_type=\"master_export\",universe_uuid=\"%s\"", univUuid);

    // Under regular load balancing or if a tserver is un-blacklisted, we expect
    // total_table_load_difference to decrease down to 0 steadily.
    // When a tserver is blacklisted, we expect tablets_in_wrong_placement to decrease steadily
    // We use total_table_diff / (-1 * deriv(total_table_diff[2m])) to estimate the rate of
    // convergence.
    // This only works if total_table_diff is going to go down to 0 but that doesn't always happen
    // (if the number of
    // tservers per AZ is imbalanced, for example), so this
    // is a rough guess.
    // tablets_in_wrong_placement does always go down to 0, so that case is more
    // accurate.
    final String promQuery =
        String.format(
            "max ((total_table_load_difference{%1$s}"
                + "  / (-1 *"
                + " deriv(total_table_load_difference{%1$s}[%2$ds])))"
                + " or (tablets_in_wrong_placement{%1$s}"
                + " / (-1 *"
                + " deriv(tablets_in_wrong_placement{%1$s}[%2$ds]))))",
            promFilters, windowDurationSecs);
    ArrayList<MetricQueryResponse.Entry> queryResult = metricQueryHelper.queryDirect(promQuery);
    log.trace("Response to is load balanced query {} is {}", promQuery, queryResult); // todo: trace
    if (queryResult.size() != 1 || queryResult.get(0).values.isEmpty()) {
      throw new RuntimeException("Unable to estimate time to balance");
    }

    double estSeconds = queryResult.get(0).values.get(0).getRight();
    if (Double.isNaN(estSeconds)
        || Double.isInfinite(estSeconds)
        || estSeconds <= 0
        || estSeconds > Duration.ofDays(10).getSeconds()) {
      throw new RuntimeException("Unable to calculate time to balance");
    }

    return Duration.ofSeconds((long) Math.ceil(estSeconds));
  }
}
