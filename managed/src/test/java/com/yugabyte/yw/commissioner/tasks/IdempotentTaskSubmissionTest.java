// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.idempotency.IdempotencyService;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.mvc.Http;
import play.mvc.Results;
import play.test.Helpers;

/** The task an idempotent request submits gets the UUID derived from its key. */
@RunWith(MockitoJUnitRunner.class)
public class IdempotentTaskSubmissionTest extends CommissionerBaseTest {

  private static final String KEY = "submit-once";

  @After
  public void cleanRequestContext() {
    RequestContext.clean(ImmutableSet.of(TokenAuthenticator.CUSTOMER));
  }

  private UUID submit() {
    // A task that fails right away: only its UUID matters here.
    AddGFlagMetadata.Params params = new AddGFlagMetadata.Params();
    return commissioner.submit(TaskType.AddGFlagMetadata, params);
  }

  @Test
  public void firstTaskOfTheRequestGetsTheDerivedUuid() throws Exception {
    RequestContext.put(TokenAuthenticator.CUSTOMER, defaultCustomer);
    Http.Request request =
        Helpers.fakeRequest("POST", "/api/v2/customers/" + defaultCustomer.getUuid() + "/things")
            .header(IdempotencyService.IDEMPOTENCY_KEY_HEADER, KEY)
            .build();
    UUID[] submitted = new UUID[2];

    app.injector()
        .instanceOf(IdempotencyService.class)
        .execute(
            request,
            () -> {
              submitted[0] = submit();
              submitted[1] = submit();
              return Results.ok();
            });

    assertEquals(IdempotencyService.taskUuidFor(defaultCustomer.getUuid(), KEY), submitted[0]);
    assertNotEquals("only the first task gets it", submitted[0], submitted[1]);
    waitForTask(submitted[0]);
    waitForTask(submitted[1]);
    assertNotNull(TaskInfo.get(submitted[0]));
  }

  @Test
  public void taskOutsideAnIdempotentRequestGetsARandomUuid() throws Exception {
    UUID taskUuid = submit();

    assertNotEquals(IdempotencyService.taskUuidFor(defaultCustomer.getUuid(), KEY), taskUuid);
    waitForTask(taskUuid);
  }
}
