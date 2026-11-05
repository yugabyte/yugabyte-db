// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.idempotency;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.ApiIdempotencyKey;
import com.yugabyte.yw.models.ApiIdempotencyKey.State;
import com.yugabyte.yw.models.Customer;
import io.ebean.DB;
import java.util.Date;
import java.util.Optional;
import java.util.UUID;
import org.apache.pekko.stream.javadsl.Source;
import org.apache.pekko.util.ByteString;
import org.junit.Before;
import org.junit.Test;
import play.http.HttpEntity;
import play.libs.Json;
import play.mvc.Result;
import play.mvc.Results;

public class IdempotencyServiceTest extends FakeDBApplication {

  private Customer customer;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
  }

  private ApiIdempotencyKey claim(String key) {
    Date now = new Date();
    ApiIdempotencyKey record = new ApiIdempotencyKey();
    record.setUuid(UUID.randomUUID());
    record.setCustomerUUID(customer.getUuid());
    record.setIdempotencyKey(key);
    record.setRequestMethod("POST");
    record.setRequestPath("/api/v2/customers/" + customer.getUuid() + "/certificates");
    record.setRequestFingerprint("f");
    record.setState(State.IN_PROGRESS);
    record.setCreateTime(now);
    record.setUpdateTime(now);
    record.setExpireTime(new Date(now.getTime() + 3600_000L));
    record.insert();
    return record;
  }

  @Test
  public void completeStoresTheResponse() {
    ApiIdempotencyKey record = claim("k");

    IdempotencyService.complete(record, Results.ok(Json.newObject().put("a", 1)));

    ApiIdempotencyKey stored = ApiIdempotencyKey.find(customer.getUuid(), "k").orElseThrow();
    assertEquals(State.COMPLETED, stored.getState());
    assertEquals(200, stored.getResponseStatus().intValue());
    assertEquals("{\"a\":1}", stored.getResponseBody());
  }

  @Test
  public void failureToStoreTheResponseReleasesTheKey() {
    ApiIdempotencyKey record = claim("k");
    // A streamed body can't be stored, so storing the response fails.
    Result streamed =
        Results.ok()
            .sendEntity(
                new HttpEntity.Streamed(
                    Source.single(ByteString.fromString("x")),
                    Optional.empty(),
                    Optional.of("text/plain")));

    IdempotencyService.complete(record, streamed);

    assertTrue(
        "the key is released, so a retry runs again",
        ApiIdempotencyKey.find(customer.getUuid(), "k").isEmpty());
  }

  @Test
  public void keyTakenOverMeanwhileIsLeftToItsNewOwner() {
    ApiIdempotencyKey record = claim("k");
    // Another request took the key over after the in-progress timeout.
    Date takenOverAt = new Date(record.getUpdateTime().getTime() + 1000);
    DB.sqlUpdate("UPDATE api_idempotency_key SET update_time = :t")
        .setParameter("t", takenOverAt)
        .execute();

    IdempotencyService.complete(record, Results.ok(Json.newObject()));

    ApiIdempotencyKey current = ApiIdempotencyKey.find(customer.getUuid(), "k").orElseThrow();
    assertEquals(State.IN_PROGRESS, current.getState());
    assertEquals(takenOverAt, current.getUpdateTime());
  }
}
