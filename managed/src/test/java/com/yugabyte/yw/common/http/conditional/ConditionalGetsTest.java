// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.http.conditional;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.util.Date;
import java.util.Optional;
import java.util.UUID;
import org.junit.Test;
import play.mvc.Http;
import play.test.Helpers;

public class ConditionalGetsTest {

  @Test
  public void weakEtagFormat() {
    UUID id = UUID.fromString("11111111-1111-1111-1111-111111111111");
    assertEquals(
        "W/\"universe:11111111-1111-1111-1111-111111111111:42\"",
        ConditionalGets.weakEtag("universe", id, 42L));
  }

  @Test
  public void scopedToYbaVersionAppendsVersion() {
    ResourceRevision rev = new ResourceRevision("W/\"universe:u:42\"", new Date(42L));
    ResourceRevision scoped = ConditionalGets.scopedToYbaVersion(rev, "2.31.0.0-b1");
    assertEquals("W/\"universe:u:42:2.31.0.0-b1\"", scoped.etag());
    assertEquals(rev.lastModified(), scoped.lastModified());
    assertEquals(rev, ConditionalGets.scopedToYbaVersion(rev, null));
  }

  @Test
  public void matchesExactWeakEtag() {
    String etag = "W/\"universe:u:1\"";
    assertTrue(ConditionalGets.matchesIfNoneMatch(etag, etag));
  }

  @Test
  public void matchesStrongAgainstWeakSameOpaque() {
    assertTrue(ConditionalGets.matchesIfNoneMatch("\"universe:u:1\"", "W/\"universe:u:1\""));
  }

  @Test
  public void matchesCommaSeparatedList() {
    assertTrue(
        ConditionalGets.matchesIfNoneMatch(
            "W/\"other:1\", W/\"universe:u:1\"", "W/\"universe:u:1\""));
  }

  @Test
  public void matchesStar() {
    assertTrue(ConditionalGets.matchesIfNoneMatch("*", "W/\"universe:u:1\""));
  }

  @Test
  public void doesNotMatchDifferentEtag() {
    assertFalse(ConditionalGets.matchesIfNoneMatch("W/\"universe:u:1\"", "W/\"universe:u:2\""));
  }

  @Test
  public void extractCustomerAndResourceFromV1AndV2Paths() {
    UUID c = UUID.fromString("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
    UUID u = UUID.fromString("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");
    String v1 = "/api/customers/" + c + "/universes/" + u;
    String v2 = "/api/v2/customers/" + c + "/universes/" + u;
    Optional<ConditionalGets.CustomerAndResource> fromV1 =
        ConditionalGets.extractCustomerAndResource(v1, "universes");
    Optional<ConditionalGets.CustomerAndResource> fromV2 =
        ConditionalGets.extractCustomerAndResource(v2, "universes");
    assertTrue(fromV1.isPresent());
    assertTrue(fromV2.isPresent());
    assertEquals(c, fromV1.get().customerUUID());
    assertEquals(u, fromV1.get().resourceUUID());
    assertEquals(c, fromV2.get().customerUUID());
    assertEquals(u, fromV2.get().resourceUUID());
  }

  @Test
  public void maybeNotModifiedReturns304() {
    UUID id = UUID.randomUUID();
    ResourceRevision rev = ConditionalGets.fromTimestamp("universe", id, new Date(1000L));
    Http.Request request =
        Helpers.fakeRequest("GET", "/x")
            .header(ConditionalGets.HEADER_IF_NONE_MATCH, rev.etag())
            .build();
    Optional<play.mvc.Result> result = ConditionalGets.maybeNotModified(request, Optional.of(rev));
    assertTrue(result.isPresent());
    assertEquals(Http.Status.NOT_MODIFIED, result.get().status());
    assertEquals(rev.etag(), result.get().headers().get(ConditionalGets.HEADER_ETAG));
  }
}
