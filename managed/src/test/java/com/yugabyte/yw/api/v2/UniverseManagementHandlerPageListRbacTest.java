// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.empty;
import static org.hamcrest.Matchers.is;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import api.v2.handlers.UniverseManagementHandler;
import api.v2.models.UniversePagedQuerySpec;
import api.v2.models.UniversePagedResp;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import java.io.IOException;
import java.util.Collections;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import play.Application;

public class UniverseManagementHandlerPageListRbacTest extends FakeDBApplication {

  private RoleBindingUtil mockRoleBindingUtil;

  @Override
  protected Application provideApplication() {
    mockRoleBindingUtil = mock(RoleBindingUtil.class);
    when(mockRoleBindingUtil.getResourceUuids(
            any(UUID.class), eq(ResourceType.UNIVERSE), eq(Action.READ)))
        .thenReturn(Collections.emptySet());
    return provideApplication(
        b -> b.overrides(bind(RoleBindingUtil.class).toInstance(mockRoleBindingUtil)));
  }

  @Before
  public void stubGFlags() throws IOException {
    when(mockGFlagsValidation.getGFlagDetails(anyString(), anyString(), anyString()))
        .thenReturn(Optional.empty());
  }

  @After
  public void clearRequestContext() {
    RequestContext.clean(Set.of(TokenAuthenticator.USER));
  }

  @Test
  public void pageListUniverses_emptyReadableSet() throws Exception {
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    TestUtils.setFakeHttpContext(user);
    createUniverse("Hidden", customer.getId());

    UniverseManagementHandler handler = app.injector().instanceOf(UniverseManagementHandler.class);
    UniversePagedQuerySpec spec = new UniversePagedQuerySpec().limit(10);
    UniversePagedResp resp = handler.pageListUniverses(customer.getUuid(), spec);
    assertThat(resp.getTotalCount(), is(0));
    assertThat(resp.getEntities(), empty());
    assertThat(resp.getHasNext(), is(false));
    assertThat(resp.getHasPrev(), is(false));
  }
}
