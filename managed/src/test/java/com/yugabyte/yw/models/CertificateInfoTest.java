// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.spy;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
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
  private CertificateHelper certificateHelper;

  private final List<String> certList = Arrays.asList("test_cert1", "test_cert2", "test_cert3");
  private final List<UUID> certIdList = new ArrayList<>();

  private final String TMP_CERTS_PATH = "/tmp/" + getClass().getSimpleName() + "/certs";

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    certificateHelper = new CertificateHelper(app.injector().instanceOf(RuntimeConfGetter.class));
    Config spyConf = spy(app.config());
    for (String cert : certList) {
      certIdList.add(certificateHelper.createRootCA(spyConf, cert, customer.getUuid()));
    }
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_CERTS_PATH));
  }

  @Test
  public void testDeleteReferencedByNodeAgent() {
    UUID certUuid = certIdList.get(0);
    File certFile = new File(CertificateInfo.get(certUuid).getCertificate());
    assertTrue(certFile.exists());
    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.setIp("10.20.30.40");
    nodeAgent.setName("node-1");
    nodeAgent.setPort(9070);
    nodeAgent.setCustomerUuid(customer.getUuid());
    nodeAgent.setOsType(NodeAgent.OSType.LINUX);
    nodeAgent.setArchType(NodeAgent.ArchType.AMD64);
    nodeAgent.setState(NodeAgent.State.READY);
    nodeAgent.setVersion("2024.2.4.0");
    nodeAgent.setHome("/home/yugabyte/node-agent");
    nodeAgent.setConfig(new NodeAgent.Config());
    nodeAgent.setCertificateUuid(certUuid);
    nodeAgent.save();

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> CertificateInfo.delete(certUuid, customer.getUuid()));
    assertEquals(BAD_REQUEST, e.getHttpStatus());
    assertTrue(e.getMessage(), e.getMessage().contains("referenced by node_agent"));
    assertNotNull(CertificateInfo.get(certUuid));
    assertTrue(certFile.exists());
  }

  @Test
  public void testDeleteUnreferenced() {
    UUID certUuid = certIdList.get(0);
    File certFile = new File(CertificateInfo.get(certUuid).getCertificate());
    CertificateInfo.delete(certUuid, customer.getUuid());
    assertNull(CertificateInfo.get(certUuid));
    assertFalse(certFile.exists());
  }

  @Test
  public void testGetAllWithNoUniverses() {
    List<CertificateInfo> certificateInfoList = CertificateInfo.getAll(customer.getUuid());
    assertEquals(3, certificateInfoList.size());
    for (CertificateInfo cert : certificateInfoList) {
      assertFalse(cert.getInUse());
      assertEquals(0, cert.getUniverseDetails().size());
      assertNotNull(cert.getStartDateIso());
    }
  }

  @Test
  public void testGetAllWithMultipleUniverses() {
    Universe universe1 =
        createUniverse(
            "Test Universe 1",
            UUID.randomUUID(),
            customer.getId(),
            Common.CloudType.aws,
            null,
            certIdList.get(0));
    createUniverse(
        "Test Universe 2",
        UUID.randomUUID(),
        customer.getId(),
        Common.CloudType.aws,
        null,
        certIdList.get(1));
    createUniverse(
        "Test Universe 3",
        UUID.randomUUID(),
        customer.getId(),
        Common.CloudType.aws,
        null,
        certIdList.get(1));

    List<CertificateInfo> certificateInfoList = CertificateInfo.getAll(customer.getUuid());
    assertEquals(3, certificateInfoList.size());
    for (CertificateInfo cert : certificateInfoList) {
      if (cert.getUuid().equals(certIdList.get(0))) {
        assertTrue(cert.getInUse());
        assertEquals(universe1.getUniverseUUID(), cert.getUniverseDetails().get(0).getUuid());
      } else if (cert.getUuid().equals(certIdList.get(1))) {
        assertTrue(cert.getInUse());
        assertEquals(2, cert.getUniverseDetails().size());
        assertNotEquals(universe1.getUniverseUUID(), cert.getUniverseDetails().get(0).getUuid());
        assertNotEquals(universe1.getUniverseUUID(), cert.getUniverseDetails().get(1).getUuid());
      } else {
        assertFalse(cert.getInUse());
        assertEquals(0, cert.getUniverseDetails().size());
      }
    }
  }

  @Test
  public void testGetAllUniverseDetailsInvocation() {
    createUniverse(
        "Test Universe 1",
        UUID.randomUUID(),
        customer.getId(),
        Common.CloudType.aws,
        null,
        certIdList.get(0));
    createUniverse(
        "Test Universe 2",
        UUID.randomUUID(),
        customer.getId(),
        Common.CloudType.aws,
        null,
        certIdList.get(1));
    createUniverse(
        "Test Universe 3",
        UUID.randomUUID(),
        customer.getId(),
        Common.CloudType.aws,
        null,
        certIdList.get(1));

    List<CertificateInfo> certificateInfoList = CertificateInfo.getAll(customer.getUuid());
    assertEquals(3, certificateInfoList.size());

    for (CertificateInfo cert : certificateInfoList) {
      // If the private fields inUse and universeDetails are not null then
      // universeDetails are already populated and won't lead to individual universe data fetch
      assertNotNull(cert.inUse);
      assertNotNull(cert.universeDetailSubsets);
    }
  }
}
