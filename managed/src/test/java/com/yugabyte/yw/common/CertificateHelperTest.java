// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.mockito.runners.MockitoJUnitRunner;


import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.UUID;
import java.util.Calendar;
import java.util.Date;
import static org.junit.Assert.*;


@RunWith(MockitoJUnitRunner.class)
public class CertificateHelperTest extends FakeDBApplication{

  private Customer c;

  private String certPath;

  @Before
  public void setUp() {
    c = ModelFactory.testCustomer();
    certPath = String.format("/tmp/certs/%s/", c.uuid);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File("/tmp/certs"));
  }

  @Test
  public void testCreateRootCA() {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "test-universe";
    UUID rootCA = CertificateHelper.createRootCA(taskParams.nodePrefix, c.uuid, "/tmp");
    assertNotNull(CertificateInfo.get(rootCA));
    try {
      InputStream in = new FileInputStream(certPath + String.format("/%s/ca.root.crt", rootCA));
      CertificateFactory factory = CertificateFactory.getInstance("X.509");
      X509Certificate cert = (X509Certificate) factory.generateCertificate(in);
      assertEquals(cert.getIssuerDN(), cert.getSubjectDN());
    } catch (Exception e){
      fail(e.getMessage());
    }
  }

  @Test
  public void testUploadRootCA() {
    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    cal.add(Calendar.YEAR, 1);
    Date certExpiry = cal.getTime();
    UUID rootCA = null;
    try {
      rootCA = CertificateHelper.uploadRootCA("test", c.uuid, "/tmp", "test_cert", "test_key",
          certStart, certExpiry);
    } catch (Exception e) {
      fail(e.getMessage());
    }
    assertNotNull(CertificateInfo.get(rootCA));
  }

}
