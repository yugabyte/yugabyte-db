// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.is;
import static org.junit.Assert.assertThrows;

import api.v2.handlers.EncryptionAtRestHandler;
import api.v2.models.KmsConfigPagedQuerySpec;
import api.v2.models.KmsConfigPagedResp;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class EncryptionAtRestHandlerTest extends FakeDBApplication {

  private Customer customer;
  private EncryptionAtRestHandler handler;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    handler = app.injector().instanceOf(EncryptionAtRestHandler.class);
  }

  KmsConfigPagedQuerySpec getSpec() {
    KmsConfigPagedQuerySpec spec = new KmsConfigPagedQuerySpec();
    spec.offset(0).limit(10);
    return spec;
  }

  @Test
  public void pageListKmsConfigs_returnsRowsWhenAuthConfigPresent() {
    KmsConfig.createKMSConfig(
        customer.getUuid(),
        KeyProvider.AWS,
        Json.newObject().put("test_key", "test_val"),
        "ear-page-test");

    KmsConfigPagedResp resp = handler.pageListKmsConfigs(customer.getUuid(), getSpec());

    assertThat(resp.getTotalCount(), is(1));
    assertThat(resp.getEntities().size(), is(1));
    assertThat(resp.getEntities().get(0).getMetadata().getName(), is("ear-page-test"));
  }

  @Test
  public void pageListKmsConfigs_returnsAllKmsConfigsForCustomer() {
    KmsConfig.createKMSConfig(
        customer.getUuid(), KeyProvider.AWS, Json.newObject().put("a", "1"), "with-auth");
    KmsConfig.createKMSConfig(
        customer.getUuid(), KeyProvider.AWS, Json.newObject().put("b", "2"), "no-auth");

    KmsConfigPagedResp resp = handler.pageListKmsConfigs(customer.getUuid(), getSpec());

    assertThat(resp.getTotalCount(), is(2));
    assertThat(resp.getEntities().size(), is(2));
  }

  @Test
  public void pageListKmsConfigs_invalidCustomer() {
    assertThrows(
        PlatformServiceException.class,
        () -> handler.pageListKmsConfigs(UUID.randomUUID(), getSpec()));
  }
}
