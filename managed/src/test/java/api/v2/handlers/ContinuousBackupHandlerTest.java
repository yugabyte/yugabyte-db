// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static play.mvc.Http.Status.BAD_REQUEST;

import api.v2.models.ContinuousBackupSpec;
import api.v2.models.TimeUnitType;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.models.ContinuousBackupConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TimeUnit;
import org.junit.Before;
import org.junit.Test;

/** A storage config YBA has no credentials for is rejected when continuous backup is set up. */
public class ContinuousBackupHandlerTest extends FakeDBApplication {

  private Customer customer;
  private CustomerConfig storageConfig;
  private BackupHelper mockBackupHelper;
  private ContinuousBackupHandler handler;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    storageConfig = ModelFactory.createS3StorageConfig(customer, "fed-s3");
    mockBackupHelper = mock(BackupHelper.class);
    doThrow(new PlatformServiceException(BAD_REQUEST, "no credentials"))
        .when(mockBackupHelper)
        .requireYbaFederatedIdentity(any(), any());
    handler =
        new ContinuousBackupHandler(
            mock(AuditService.class),
            mock(Commissioner.class),
            mock(ConfigHelper.class),
            mock(StorageUtilFactory.class),
            mockBackupHelper);
  }

  private ContinuousBackupSpec spec(String backupDir) {
    return new ContinuousBackupSpec()
        .storageConfigUuid(storageConfig.getConfigUUID())
        .frequency(5L)
        .frequencyTimeUnit(TimeUnitType.MINUTES)
        .numBackups(3)
        .backupDir(backupDir);
  }

  @Test
  public void testCreateRejectsAStorageConfigYbaCannotReach() {
    assertThrows(
        PlatformServiceException.class,
        () -> handler.createContinuousBackup(customer.getUuid(), spec("test")));

    assertFalse(ContinuousBackupConfig.get().isPresent());
  }

  @Test
  public void testEditRejectsAStorageConfigYbaCannotReach() {
    ContinuousBackupConfig existing =
        ContinuousBackupConfig.create(
            storageConfig.getConfigUUID(), 5, TimeUnit.MINUTES, 3, "before");

    assertThrows(
        PlatformServiceException.class,
        () -> handler.editContinuousBackup(customer.getUuid(), existing.getUuid(), spec("after")));

    assertEquals("before", ContinuousBackupConfig.get(existing.getUuid()).get().getBackupDir());
  }

  @Test
  public void testConfigBeingValidatedIsRejected() {
    // Created through v2 and still being checked: it is deleted if its check fails.
    storageConfig.setState(CustomerConfig.ConfigState.Validating);
    storageConfig.update();

    PlatformServiceException create =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.createContinuousBackup(customer.getUuid(), spec("test")));
    assertEquals(BAD_REQUEST, create.getHttpStatus());
    assertTrue(create.getMessage(), create.getMessage().contains("only an active storage config"));
    assertFalse(ContinuousBackupConfig.get().isPresent());

    ContinuousBackupConfig existing =
        ContinuousBackupConfig.create(
            ModelFactory.createS3StorageConfig(customer, "active").getConfigUUID(),
            5,
            TimeUnit.MINUTES,
            3,
            "before");
    PlatformServiceException edit =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.editContinuousBackup(
                    customer.getUuid(), existing.getUuid(), spec("after")));
    assertTrue(edit.getMessage(), edit.getMessage().contains("only an active storage config"));
  }
}
