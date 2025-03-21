// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.operator.utils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.nullable;
import static org.mockito.Mockito.doCallRealMethod;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TimeUnit;
import io.fabric8.kubernetes.client.Config;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.CommonTypes.TableType;
import play.data.FormFactory;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class OperatorUtilsTest extends FakeDBApplication {

  private RuntimeConfGetter mockConfGetter;
  private Config k8sClientConfig;
  private YbcManager mockYbcManager;
  private ValidatingFormFactory mockValidatingFormFactory;
  private BeanValidator mockBeanValidator;
  private FormFactory mockFormFactory;
  private ReleaseManager mockReleaseManager;
  private OperatorUtils operatorUtils;

  private Universe testUniverse;
  private Customer testCustomer;
  private CustomerConfig testStorageConfig;
  private ObjectMapper mapper;

  @Before
  public void setup() throws Exception {
    mockConfGetter = Mockito.mock(RuntimeConfGetter.class);
    mockYbcManager = Mockito.mock(YbcManager.class);
    mockFormFactory = Mockito.mock(FormFactory.class);
    mockBeanValidator = Mockito.mock(BeanValidator.class);
    mockValidatingFormFactory = spy(new ValidatingFormFactory(mockFormFactory, mockBeanValidator));
    doCallRealMethod()
        .when(mockValidatingFormFactory)
        .getFormDataOrBadRequest(any(JsonNode.class), any());
    mockReleaseManager = Mockito.mock(ReleaseManager.class);
    operatorUtils =
        spy(
            new OperatorUtils(
                mockConfGetter, mockReleaseManager, mockYbcManager, mockValidatingFormFactory));

    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse("operator-universe", testCustomer.getId());
    testStorageConfig = ModelFactory.createS3StorageConfig(testCustomer, "operator-storage");
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorCustomerUUID))
        .thenReturn(testCustomer.getUuid().toString());
    mapper = new ObjectMapper();
  }

  private ObjectNode getScheduleBackupParamsJson() {
    ObjectNode spec = Json.newObject();
    spec.put("keyspace", "testdb");
    spec.put("backupType", "PGSQL_TABLE_TYPE");
    spec.put("storageConfig", "operator-storage");
    spec.put("universe", "operator-universe");
    spec.put("schedulingFrequency", "3600000");
    spec.put("incrementalBackupFrequency", "900000");
    return spec;
  }

  private ObjectNode getIncrementalBackupParamsJson() {
    ObjectNode spec = Json.newObject();
    spec.put("keyspace", "testdb");
    spec.put("backupType", "PGSQL_TABLE_TYPE");
    spec.put("storageConfig", "operator-storage");
    spec.put("universe", "operator-universe");
    spec.put("incrementalBackupBase", "full-backup");
    return spec;
  }

  @Test
  public void testGenerateBackupParamsScheduledBackupSuccess() throws Exception {
    doReturn(testStorageConfig.getConfigUUID())
        .when(operatorUtils)
        .getStorageConfigUUIDFromName(anyString(), nullable(SharedIndexInformer.class));
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));

    ObjectNode spec = getScheduleBackupParamsJson();
    BackupRequestParams scheduleParams = operatorUtils.getBackupRequestFromCr(spec, null, null);
    assertEquals(scheduleParams.schedulingFrequency, 3600000);
    assertEquals(scheduleParams.frequencyTimeUnit, TimeUnit.MILLISECONDS);
    assertEquals(scheduleParams.incrementalBackupFrequency, 900000);
    assertEquals(scheduleParams.incrementalBackupFrequencyTimeUnit, TimeUnit.MILLISECONDS);
    assertEquals(scheduleParams.getUniverseUUID(), testUniverse.getUniverseUUID());
    assertEquals(scheduleParams.storageConfigUUID, testStorageConfig.getConfigUUID());
    assertEquals(scheduleParams.keyspaceTableList.size(), 1);
    assertEquals(scheduleParams.keyspaceTableList.get(0).keyspace, "testdb");
    assertEquals(scheduleParams.backupType, TableType.PGSQL_TABLE_TYPE);
  }

  @Test
  public void testGenerateBackupParamsScheduledBackupFailUniverseNotFound() throws Exception {
    doReturn(null)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));

    ObjectNode spec = getScheduleBackupParamsJson();
    Exception ex =
        assertThrows(Exception.class, () -> operatorUtils.getBackupRequestFromCr(spec, null, null));
    assertEquals(ex.getMessage(), "No universe found with name operator-universe");
  }

  @Test
  public void testGenerateBackupParamsScheduledBackupFailStorageConfigNotFound() throws Exception {
    doReturn(null)
        .when(operatorUtils)
        .getStorageConfigUUIDFromName(anyString(), nullable(SharedIndexInformer.class));
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));

    ObjectNode spec = getScheduleBackupParamsJson();
    Exception ex =
        assertThrows(Exception.class, () -> operatorUtils.getBackupRequestFromCr(spec, null, null));
    assertEquals(ex.getMessage(), "No storage config found with name operator-storage");
  }

  @Test
  public void testGenerateBackupParamsIncrementalBackupSuccess() throws Exception {
    doReturn(UUID.fromString(testStorageConfig.getConfigUUID().toString()))
        .when(operatorUtils)
        .getStorageConfigUUIDFromName(anyString(), nullable(SharedIndexInformer.class));
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));
    Backup backup =
        ModelFactory.createBackup(
            testCustomer.getUuid(),
            testUniverse.getUniverseUUID(),
            testStorageConfig.getConfigUUID());
    doReturn(backup)
        .when(operatorUtils)
        .getBaseBackup(anyString(), nullable(String.class), any(Customer.class));
    backup.setState(BackupState.Completed);
    backup.save();

    ObjectNode spec = getIncrementalBackupParamsJson();
    BackupRequestParams backupParams = operatorUtils.getBackupRequestFromCr(spec, null, null);
    assertEquals(backupParams.getUniverseUUID(), testUniverse.getUniverseUUID());
    assertEquals(backupParams.storageConfigUUID, testStorageConfig.getConfigUUID());
    assertEquals(backupParams.keyspaceTableList.size(), 1);
    assertEquals(backupParams.keyspaceTableList.get(0).keyspace, "testdb");
    assertEquals(backupParams.backupType, TableType.PGSQL_TABLE_TYPE);
    assertEquals(backupParams.baseBackupUUID, backup.getBackupUUID());
  }

  @Test
  public void testGenerateBackupParamsIncrementalBackupDifferentStorageConfig() throws Exception {
    doReturn(testStorageConfig.getConfigUUID())
        .when(operatorUtils)
        .getStorageConfigUUIDFromName(anyString(), nullable(SharedIndexInformer.class));
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));
    // Different storageConfig
    CustomerConfig config_2 = ModelFactory.createS3StorageConfig(testCustomer, "test-2");
    Backup backup =
        ModelFactory.createBackup(
            testCustomer.getUuid(), testUniverse.getUniverseUUID(), config_2.getConfigUUID());
    doReturn(backup)
        .when(operatorUtils)
        .getBaseBackup(anyString(), nullable(String.class), any(Customer.class));
    backup.setState(BackupState.Completed);
    backup.save();

    ObjectNode spec = getIncrementalBackupParamsJson();
    Exception ex =
        assertThrows(Exception.class, () -> operatorUtils.getBackupRequestFromCr(spec, null, null));
    assertEquals(
        ex.getMessage(),
        "Invalid cr values: Storage config and Universe should be same for incremental backup");
  }

  @Test
  public void testGenerateBackupParamsIncrementalBackupDifferentUniverse() throws Exception {
    doReturn(testStorageConfig.getConfigUUID())
        .when(operatorUtils)
        .getStorageConfigUUIDFromName(anyString(), nullable(SharedIndexInformer.class));
    doReturn(testUniverse)
        .when(operatorUtils)
        .getUniverseFromNameAndNamespace(anyLong(), anyString(), nullable(String.class));
    // Different storageConfig
    Universe testUniverse_2 = ModelFactory.createUniverse("test-2", testCustomer.getId());
    Backup backup =
        ModelFactory.createBackup(
            testCustomer.getUuid(),
            testUniverse_2.getUniverseUUID(),
            testStorageConfig.getConfigUUID());
    doReturn(backup)
        .when(operatorUtils)
        .getBaseBackup(anyString(), nullable(String.class), any(Customer.class));
    backup.setState(BackupState.Completed);
    backup.save();

    ObjectNode spec = getIncrementalBackupParamsJson();
    Exception ex =
        assertThrows(Exception.class, () -> operatorUtils.getBackupRequestFromCr(spec, null, null));
    assertEquals(
        ex.getMessage(),
        "Invalid cr values: Storage config and Universe should be same for incremental backup");
  }
}
