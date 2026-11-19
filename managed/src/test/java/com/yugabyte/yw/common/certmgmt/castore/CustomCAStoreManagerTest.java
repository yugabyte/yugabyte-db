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
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.nio.file.Files;
import java.security.KeyStore;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
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

  private static final String BUNDLE =
      TestUtils.readResource("certs/fips/root-sha256.crt")
          + TestUtils.readResource("certs/fips/root-ec-p256.crt")
          + TestUtils.readResource("certs/fips/intermediate-sha256.crt");

  private TrustStoreManager.TrustStoreInfo trustStoreInfo() {
    return app.injector()
        .instanceOf(YBATrustStoreManager.class)
        .getYbaTrustStoreInfo(storagePath + "/certs/trust-store");
  }

  private KeyStore loadTrustStore() throws Exception {
    TrustStoreManager.TrustStoreInfo info = trustStoreInfo();
    KeyStore trustStore = KeyStore.getInstance(info.getType());
    try (FileInputStream in = new FileInputStream(info.getPath())) {
      trustStore.load(in, info.getPassword().toCharArray());
    }
    return trustStore;
  }

  private List<String> aliases(String name) throws Exception {
    return Collections.list(loadTrustStore().aliases()).stream()
        .filter(alias -> alias.startsWith(name + "-"))
        .sorted()
        .toList();
  }

  @Test
  public void testAddCACertStoresEveryCertificateOfABundle() throws Exception {
    customCAStoreManager.addCACert(customer.getUuid(), "bundle", BUNDLE, storagePath);
    assertEquals(List.of("bundle-0", "bundle-1", "bundle-2"), aliases("bundle"));
  }

  @Test
  public void testAddMissingTrustStoreCertificates() throws Exception {
    UUID certId = customCAStoreManager.addCACert(customer.getUuid(), "bundle", BUNDLE, storagePath);
    // Leave only the first certificate, as uploads before PLAT-23769 did.
    KeyStore trustStore = loadTrustStore();
    trustStore.deleteEntry("bundle-1");
    trustStore.deleteEntry("bundle-2");
    TrustStoreManager.TrustStoreInfo info = trustStoreInfo();
    try (FileOutputStream out = new FileOutputStream(info.getPath())) {
      trustStore.store(out, info.getPassword().toCharArray());
    }

    customCAStoreManager.addMissingTrustStoreCertificates(storagePath);
    assertEquals(List.of("bundle-0", "bundle-1", "bundle-2"), aliases("bundle"));

    // A CA repaired this way can be updated and deleted.
    String newContents =
        TestUtils.readResource("certs/fips/root-sha256.crt")
            + TestUtils.readResource("certs/fips/root-ec-p256.crt");
    UUID newCertId =
        customCAStoreManager.updateCA(
            customer.getUuid(), certId, "bundle", newContents, storagePath);
    assertEquals(List.of("bundle-0", "bundle-1"), aliases("bundle"));
    assertTrue(customCAStoreManager.deleteCA(customer.getUuid(), newCertId, storagePath));
    assertEquals(List.of(), aliases("bundle"));
  }
}
