// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.certmgmt.castore;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mockStatic;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.models.CustomCaCertificateInfo;
import com.yugabyte.yw.models.Customer;
import java.nio.file.Files;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.mockito.MockedStatic;

public class CustomCAStoreManagerTest extends FakeDBApplication {

  private Customer customer;
  private CustomCAStoreManager customCAStoreManager;
  private String storagePath;

  @Before
  public void setUp() throws Exception {
    customer = ModelFactory.testCustomer();
    customCAStoreManager = app.injector().instanceOf(CustomCAStoreManager.class);
    storagePath = Files.createTempDirectory("castore").toString();
  }

  @Test
  public void testAddCACertRejectsKeyTypesFipsDoesNotAllow() {
    try (MockedStatic<AppConfigHelper> appConfig = mockStatic(AppConfigHelper.class)) {
      appConfig.when(AppConfigHelper::isFipsEnabled).thenReturn(true);
      for (String root : List.of("root-ec-p192.crt", "root-dsa.crt", "root-ed25519.crt")) {
        String contents = TestUtils.readResource("certs/fips/" + root);
        PlatformServiceException e =
            assertThrows(
                PlatformServiceException.class,
                () ->
                    customCAStoreManager.addCACert(
                        customer.getUuid(), root, contents, storagePath));
        assertEquals(BAD_REQUEST, e.getHttpStatus());
        assertTrue(root + ": " + e.getMessage(), e.getMessage().contains("FIPS mode requires"));
        assertNull(CustomCaCertificateInfo.getByName(root));
      }
    }
  }

  @Test
  public void testAddCACertAcceptsWeakKeyWhenFipsDisabled() {
    String contents = TestUtils.readResource("certs/fips/root-ec-p192.crt");
    assertNotNull(
        customCAStoreManager.addCACert(customer.getUuid(), "p192", contents, storagePath));
  }
}
