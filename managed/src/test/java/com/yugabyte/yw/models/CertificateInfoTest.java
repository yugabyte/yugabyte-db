// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class CertificateInfoTest extends FakeDBApplication {

  private Customer customer;

  private final List<String> certList = Arrays.asList("test_cert1", "test_cert2", "test_cert3");
  private final List<UUID> certIdList = new ArrayList<>();

  private final String TMP_CERTS_PATH = "/tmp/" + getClass().getSimpleName() + "/certs";

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    Config spyConf = spy(app.config());
    doReturn(TMP_CERTS_PATH).when(spyConf).getString("yb.storage.path");
    for (String cert : certList) {
      certIdList.add(CertificateHelper.createRootCA(spyConf, cert, customer.uuid));
    }
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_CERTS_PATH));
  }

  @Test
  public void testGetAllWithNoUniverses() {
    List<CertificateInfo> certificateInfoList = CertificateInfo.getAll(customer.uuid);
    assertEquals(3, certificateInfoList.size());
    for (CertificateInfo cert : certificateInfoList) {
      assertFalse(cert.getInUse());
      assertEquals(0, cert.getUniverseDetails().size());
    }
  }

  @Test
  public void testGetAllWithMultipleUniverses() {
    Universe universe1 =
        createUniverse(
            "Test Universe 1",
            UUID.randomUUID(),
            customer.getCustomerId(),
            Common.CloudType.aws,
            null,
            certIdList.get(0));
    createUniverse(
        "Test Universe 2",
        UUID.randomUUID(),
        customer.getCustomerId(),
        Common.CloudType.aws,
        null,
        certIdList.get(1));
    createUniverse(
        "Test Universe 3",
        UUID.randomUUID(),
        customer.getCustomerId(),
        Common.CloudType.aws,
        null,
        certIdList.get(1));

    List<CertificateInfo> certificateInfoList = CertificateInfo.getAll(customer.uuid);
    assertEquals(3, certificateInfoList.size());
    for (CertificateInfo cert : certificateInfoList) {
      if (cert.uuid.equals(certIdList.get(0))) {
        assertTrue(cert.getInUse());
        assertEquals(universe1.universeUUID, cert.getUniverseDetails().get(0).getUuid());
      } else if (cert.uuid.equals(certIdList.get(1))) {
        assertTrue(cert.getInUse());
        assertEquals(2, cert.getUniverseDetails().size());
        assertNotEquals(universe1.universeUUID, cert.getUniverseDetails().get(0).getUuid());
        assertNotEquals(universe1.universeUUID, cert.getUniverseDetails().get(1).getUuid());
      } else {
        assertFalse(cert.getInUse());
        assertEquals(0, cert.getUniverseDetails().size());
      }
    }
  }

  @Test
  public void testGetAllUniverseDetailsInvocation()
      throws NoSuchFieldException, IllegalAccessException {
    createUniverse(
        "Test Universe 1",
        UUID.randomUUID(),
        customer.getCustomerId(),
        Common.CloudType.aws,
        null,
        certIdList.get(0));
    createUniverse(
        "Test Universe 2",
        UUID.randomUUID(),
        customer.getCustomerId(),
        Common.CloudType.aws,
        null,
        certIdList.get(1));
    createUniverse(
        "Test Universe 3",
        UUID.randomUUID(),
        customer.getCustomerId(),
        Common.CloudType.aws,
        null,
        certIdList.get(1));

    List<CertificateInfo> certificateInfoList = CertificateInfo.getAll(customer.uuid);
    assertEquals(3, certificateInfoList.size());

    for (CertificateInfo cert : certificateInfoList) {
      // If the private fields inUse and universeDetails are not null then
      // universeDetails are already populated and won't lead to individual universe data fetch
      assertNotNull(cert.inUse);
      assertNotNull(cert.universeDetailSubsets);
    }
  }
}
