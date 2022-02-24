package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class DeleteCertificateTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private CertificateInfo usedCertificateInfo, unusedCertificateInfo;
  private Universe universe;
  private File certFolder;

  @Mock private BaseTaskDependencies baseTaskDependencies;
  @Mock private RuntimeConfigFactory runtimeConfigFactory;

  private DeleteCertificate.Params params;
  private DeleteCertificate task;

  @Before
  public void setUp() throws IOException, NoSuchAlgorithmException {
    String certificate = createTempFile("delete_certificate_test", "ca.crt", "test data");

    certFolder = new File(certificate).getParentFile();
    defaultCustomer = ModelFactory.testCustomer();
    usedCertificateInfo =
        ModelFactory.createCertificateInfo(
            defaultCustomer.getUuid(), certificate, CertConfigType.SelfSigned);
    universe =
        ModelFactory.createUniverse(defaultCustomer.getCustomerId(), usedCertificateInfo.uuid);

    unusedCertificateInfo =
        ModelFactory.createCertificateInfo(
            defaultCustomer.getUuid(), certificate, CertConfigType.SelfSigned);

    params = new DeleteCertificate.Params();
    params.customerUUID = defaultCustomer.uuid;
    task = AbstractTaskBase.createTask(DeleteCertificate.class);
  }

  @After
  public void tearDown() {
    universe.delete();
    defaultCustomer.delete();
    usedCertificateInfo.delete();
    unusedCertificateInfo.delete();
  }

  @Test
  public void testDeleteCertificateInUse() {
    params.certUUID = usedCertificateInfo.uuid;
    task.initialize(params);
    task.run();
    assertTrue(certFolder.exists());
  }

  @Test
  public void testDeleteCertificateNotInUse() {
    params.certUUID = unusedCertificateInfo.uuid;
    task.initialize(params);
    task.run();
    assertTrue(!certFolder.exists());
  }
}
