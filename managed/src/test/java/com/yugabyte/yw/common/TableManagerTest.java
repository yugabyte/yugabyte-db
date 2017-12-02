// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.forms.BulkImportParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;

import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import static com.yugabyte.yw.common.TableManager.BULK_LOAD_SCRIPT;
import static com.yugabyte.yw.common.TableManager.PY_WRAPPER;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class TableManagerTest extends FakeDBApplication {

  @Mock
  play.Configuration mockAppConfig;

  @Mock
  ShellProcessHandler shellProcessHandler;

  @Mock
  ReleaseManager releaseManager;

  @InjectMocks
  TableManager tableManager;

  private Provider testProvider;
  private Customer testCustomer;
  private Universe testUniverse;
  private String keyCode = "demo-access";
  private String pkPath = "/path/to/private.key";

  private List<UUID> getMockRegionUUIDs(int numRegions) {
    List<UUID> regionUUIDs = new LinkedList<>();
    for (int i = 0; i < numRegions; ++i) {
      String regionCode = "region-" + Integer.toString(i);
      String regionName = "Foo Region " + Integer.toString(i);
      String azCode = "PlacementAZ-" + Integer.toString(i);
      String azName = "PlacementAZ " + Integer.toString(i);
      String subnetName = "Subnet - " + Integer.toString(i);
      Region r = Region.create(testProvider, regionCode, regionName, "default-image");
      AvailabilityZone.create(r, azCode, azName, subnetName);
      regionUUIDs.add(r.uuid);
    }
    return regionUUIDs;
  }

  private void buildValidParams(BulkImportParams bulkImportParams,
                                UniverseDefinitionTaskParams uniParams) {
    // Set up accessKey
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.privateKey = pkPath;
    if (AccessKey.get(testProvider.uuid, keyCode) == null) {
      AccessKey.create(testProvider.uuid, keyCode, keyInfo);
    }

    bulkImportParams.tableName = "mock_table";
    bulkImportParams.keyspace = "mock_ks";
    bulkImportParams.s3Bucket = "s3://foo.bar.com/bulkload";
    bulkImportParams.universeUUID = testUniverse.universeUUID;

    uniParams.nodePrefix = "yb-1-" + testUniverse.name;
    UserIntent userIntent = new UniverseDefinitionTaskParams.UserIntent();
    userIntent.accessKeyCode = keyCode;
    userIntent.ybSoftwareVersion = "0.0.1";
    userIntent.numNodes = 3;
    userIntent.replicationFactor = 3;
    userIntent.isMultiAZ = true;
    userIntent.regionList = getMockRegionUUIDs(3);
    uniParams.upsertPrimaryCluster(userIntent, null);
  }

  private List<String> getExpectedCommmand(BulkImportParams bulkImportParams,
                                           UniverseDefinitionTaskParams uniParams) {
    List<String> cmd = new LinkedList<>();
    // bin/py_wrapper bin/yb_bulk_load.py, --key_path, /path/to/private.key, --instance_count, 24,
    // --universe, Universe-1, --release, /yb/release.tar.gz,
    // --masters, host-n1:7100,host-n2:7100,host-n3:7100, --table, mock_table, --keyspace, mock_ks,
    // --s3bucket, s3://foo.bar.com/bulkload
    cmd.add(PY_WRAPPER);
    cmd.add(BULK_LOAD_SCRIPT);
    cmd.add("--key_path");
    cmd.add(pkPath);
    cmd.add("--instance_count");
    if (bulkImportParams.instanceCount == 0) {
      cmd.add(Integer.toString(uniParams.retrievePrimaryCluster().userIntent.numNodes * 8));
    } else {
      cmd.add(Integer.toString(bulkImportParams.instanceCount));
    }
    cmd.add("--universe");
    cmd.add("yb-1-" + testUniverse.name);
    cmd.add("--release");
    cmd.add("/yb/release.tar.gz");
    cmd.add("--masters");
    cmd.add(testUniverse.getMasterAddresses());
    cmd.add("--table");
    cmd.add(bulkImportParams.tableName);
    cmd.add("--keyspace");
    cmd.add(bulkImportParams.keyspace);
    cmd.add("--s3bucket");
    cmd.add(bulkImportParams.s3Bucket);
    return cmd;
  }

  @Before
  public void setUp() {
    testCustomer = ModelFactory.testCustomer();
    testProvider = ModelFactory.awsProvider(testCustomer);
    testUniverse = Universe.create("Universe-1", UUID.randomUUID(), testCustomer.getCustomerId());
    testCustomer.addUniverseUUID(testUniverse.universeUUID);
    testCustomer.save();
    when(mockAppConfig.getString("yb.devops.home")).thenReturn("/my/devops");
    when(releaseManager.getReleaseByVersion("0.0.1")).thenReturn("/yb/release.tar.gz");
  }

  @Test
  public void testTableCommandWithDefaultInstanceCount() {
    BulkImportParams bulkImportParams = new BulkImportParams();
    UniverseDefinitionTaskParams uniTaskParams = new UniverseDefinitionTaskParams();
    buildValidParams(bulkImportParams, uniTaskParams);
    UserIntent userIntent = uniTaskParams.retrievePrimaryCluster().userIntent;
    testUniverse.setUniverseDetails(uniTaskParams);
    testUniverse = Universe.saveDetails(testUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(userIntent, uniTaskParams.nodePrefix));

    List<String> expectedCommand = getExpectedCommmand(bulkImportParams, uniTaskParams);
    Map<String, String> expectedEnvVars = testProvider.getConfig();
    expectedEnvVars.put("AWS_DEFAULT_REGION", Region.get(userIntent.regionList.get(0)).code);

    tableManager.tableCommand(bulkImportParams);
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }

  @Test
  public void testTableCommandWithSpecificInstanceCount() {
    BulkImportParams bulkImportParams = new BulkImportParams();
    bulkImportParams.instanceCount = 5;
    UniverseDefinitionTaskParams uniTaskParams = new UniverseDefinitionTaskParams();
    buildValidParams(bulkImportParams, uniTaskParams);
    UserIntent userIntent = uniTaskParams.retrievePrimaryCluster().userIntent;
    testUniverse.setUniverseDetails(uniTaskParams);
    testUniverse = Universe.saveDetails(testUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(userIntent, uniTaskParams.nodePrefix));

    List<String> expectedCommand = getExpectedCommmand(bulkImportParams, uniTaskParams);
    Map<String, String> expectedEnvVars = testProvider.getConfig();
    expectedEnvVars.put("AWS_DEFAULT_REGION", Region.get(userIntent.regionList.get(0)).code);

    tableManager.tableCommand(bulkImportParams);
    verify(shellProcessHandler, times(1)).run(expectedCommand, expectedEnvVars);
  }
}
