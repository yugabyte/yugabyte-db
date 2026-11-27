// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.rbac.handlers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.util.UUID;
import org.junit.Test;

public class RequestPathsTest {

  private static final UUID A = UUID.fromString("f1111111-1111-4111-8111-111111111111");
  private static final UUID B = UUID.fromString("f2222222-2222-4222-8222-222222222222");

  @Test
  public void testUuidAfterTakesFirstMatch() {
    String path =
        String.format("/api/v1/customers/%s/pa_proxy/%s/api/x/customers/%s/universes", A, B, B);
    assertEquals(A, RequestPaths.uuidAfter(path, "customers"));
  }

  @Test
  public void testUuidAfterDecodesValue() {
    String encoded = "%66" + A.toString().substring(1);
    assertEquals(A, RequestPaths.uuidAfter("/api/v1/customers/" + encoded + "/users", "customers"));
  }

  @Test
  public void testUuidAfterWithoutUuid() {
    assertNull(RequestPaths.uuidAfter("/api/v1/customers", "customers"));
    assertNull(RequestPaths.uuidAfter("/api/v1/customers/" + A + "/universes/list", "universes"));
    assertNull(RequestPaths.uuidAfter("/api/v1/xcustomers/" + A, "customers"));
    assertNull(RequestPaths.uuidAfter("/api/v1/%63ustomers/" + A, "customers"));
  }

  @Test
  public void testIsMetricsProxy() {
    assertTrue(RequestPaths.isMetricsProxy("/universes/" + A + "/proxy/10.0.0.1:9000/metrics"));
    assertFalse(
        RequestPaths.isMetricsProxy(
            String.format(
                "/api/v1/customers/%s/pa_proxy/%s/api/x/universes/%s/proxy/h:9000/", A, B, A)));
  }
}
