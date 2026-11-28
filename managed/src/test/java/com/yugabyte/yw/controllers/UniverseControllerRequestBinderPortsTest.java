// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseTaskParams.CommunicationPorts;
import org.junit.Test;
import play.libs.Json;

public class UniverseControllerRequestBinderPortsTest {

  // The merge applies every port the request sends and keeps the current value for the rest.
  @Test
  public void testMergePortsKeepsPortsTheRequestLeavesOut() {
    CommunicationPorts current = new CommunicationPorts();
    current.masterHttpPort = 7000;
    current.otelCollectorMetricsPort = 8899;
    current.internalYsqlServerRpcPort = 6440;

    Object[][] cases = {
      // request communicationPorts, master HTTP, otel, internal YSQL RPC
      {"{\"masterHttpPort\": 7011}", 7011, 8899, 6440},
      {"{\"otelCollectorMetricsPort\": 8889}", 7000, 8889, 6440},
      {"{\"otelCollectorMetricsPort\": null}", 7000, 8899, 6440},
      {"{}", 7000, 8899, 6440},
      {null, 7000, 8899, 6440},
    };
    for (Object[] c : cases) {
      String requested = (String) c[0];
      CommunicationPorts merged =
          Json.fromJson(
              UniverseControllerRequestBinder.mergePorts(
                  current, requested == null ? null : Json.parse(requested)),
              CommunicationPorts.class);

      assertEquals(requested, c[1], merged.masterHttpPort);
      assertEquals(requested, c[2], merged.otelCollectorMetricsPort);
      assertEquals(requested, c[3], merged.internalYsqlServerRpcPort);
    }
  }

  // A communicationPorts value that is not an object or null gives 400, as binding did before.
  @Test
  public void testMergePortsRejectsNonObject() {
    for (String requested : new String[] {"\"abc\"", "[1]", "7000"}) {
      PlatformServiceException e =
          assertThrows(
              requested,
              PlatformServiceException.class,
              () ->
                  UniverseControllerRequestBinder.mergePorts(
                      new CommunicationPorts(), Json.parse(requested)));
      assertEquals(requested, BAD_REQUEST, e.getHttpStatus());
    }
  }
}
