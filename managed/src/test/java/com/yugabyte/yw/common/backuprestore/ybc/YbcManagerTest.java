// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.backuprestore.ybc;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.nullable;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.YbcThrottleParameters;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.YbcClient;
import org.yb.ybc.BackupServiceValidateCloudConfigRequest;
import org.yb.ybc.BackupServiceValidateCloudConfigResponse;
import org.yb.ybc.CloudStoreConfig;
import org.yb.ybc.ControllerStatus;
import org.yb.ybc.NonRestartSettableControllerFlags;
import org.yb.ybc.RpcControllerStatus;

@RunWith(JUnitParamsRunner.class)
public class YbcManagerTest extends FakeDBApplication {

  private CustomerConfigService mockCustomerConfigService;
  private RuntimeConfGetter mockConfGetter;
  private KubernetesManagerFactory mockKubernetesManagerFactory;
  private YbcManager spyYbcManager;
  private YbcManager ybcManager;

  private Universe testUniverse;
  private Customer testCustomer;

  @Before
  public void setup() {
    mockCustomerConfigService = mock(CustomerConfigService.class);
    mockConfGetter = mock(RuntimeConfGetter.class);
    mockKubernetesManagerFactory = mock(KubernetesManagerFactory.class);
    ybcManager =
        new YbcManager(
            mockYbcClientService,
            mockCustomerConfigService,
            mockConfGetter,
            mockReleaseManager,
            mockNodeManager,
            mockKubernetesManagerFactory,
            mockFileHelperService,
            mockStorageUtilFactory,
            mockGFlagsValidation);
    spyYbcManager = spy(ybcManager);
    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse(testCustomer.getId());
    ModelFactory.addNodesToUniverse(testUniverse.getUniverseUUID(), 3);
  }

  @Test
  public void testGetYbcClientIpPair() {
    YbcClient mockYbcClient_Ip1 = mock(YbcClient.class);
    YbcClient mockYbcClient_Ip2 = mock(YbcClient.class);
    YbcClient mockYbcClient_Ip3 = mock(YbcClient.class);
    lenient()
        .when(mockYbcClientService.getNewClient(eq("127.0.0.1"), anyInt(), eq(null)))
        .thenReturn(mockYbcClient_Ip1);
    lenient()
        .when(mockYbcClientService.getNewClient(eq("127.0.0.2"), anyInt(), eq(null)))
        .thenReturn(mockYbcClient_Ip2);
    lenient()
        .when(mockYbcClientService.getNewClient(eq("127.0.0.3"), anyInt(), eq(null)))
        .thenReturn(mockYbcClient_Ip3);
    when(spyYbcManager.ybcPingCheck(any(YbcClient.class))).thenReturn(true);
    Pair<YbcClient, String> clientIpPair =
        spyYbcManager.getAvailableYbcClientIpPair(testUniverse.getUniverseUUID(), null);
    assert (clientIpPair.getFirst() != null && StringUtils.isNotBlank(clientIpPair.getSecond()));
    assert (new HashSet<>(Arrays.asList(mockYbcClient_Ip1, mockYbcClient_Ip2, mockYbcClient_Ip3))
        .contains(clientIpPair.getFirst()));
  }

  @Test
  public void testGetYbcClientIpPairOneHealthyNode() {
    YbcClient mockYbcClient_Ip1 = mock(YbcClient.class);
    YbcClient mockYbcClient_Ip2 = mock(YbcClient.class);
    YbcClient mockYbcClient_Ip3 = mock(YbcClient.class);
    lenient()
        .when(mockYbcClientService.getNewClient(eq("127.0.0.1"), anyInt(), eq(null)))
        .thenReturn(mockYbcClient_Ip1);
    lenient()
        .when(mockYbcClientService.getNewClient(eq("127.0.0.2"), anyInt(), eq(null)))
        .thenReturn(mockYbcClient_Ip2);
    lenient()
        .when(mockYbcClientService.getNewClient(eq("127.0.0.3"), anyInt(), eq(null)))
        .thenReturn(mockYbcClient_Ip3);

    when(spyYbcManager.ybcPingCheck(eq(mockYbcClient_Ip2))).thenReturn(true);
    Pair<YbcClient, String> clientIpPair =
        spyYbcManager.getAvailableYbcClientIpPair(testUniverse.getUniverseUUID(), null);
    assert (clientIpPair.getFirst() != null && StringUtils.isNotBlank(clientIpPair.getSecond()));
    assert (clientIpPair.getFirst().equals(mockYbcClient_Ip2));
  }

  @Test
  public void testGetYbcClientIpPairNoHealthyNodes() {
    YbcClient mockYbcClient_Ip1 = mock(YbcClient.class);
    YbcClient mockYbcClient_Ip2 = mock(YbcClient.class);
    YbcClient mockYbcClient_Ip3 = mock(YbcClient.class);
    when(mockYbcClientService.getNewClient(eq("127.0.0.1"), anyInt(), eq(null)))
        .thenReturn(mockYbcClient_Ip1);
    when(mockYbcClientService.getNewClient(eq("127.0.0.2"), anyInt(), eq(null)))
        .thenReturn(mockYbcClient_Ip2);
    when(mockYbcClientService.getNewClient(eq("127.0.0.3"), anyInt(), eq(null)))
        .thenReturn(mockYbcClient_Ip3);

    when(spyYbcManager.ybcPingCheck(any(YbcClient.class))).thenReturn(false);
    assertThrows(
        RuntimeException.class,
        () -> spyYbcManager.getAvailableYbcClientIpPair(testUniverse.getUniverseUUID(), null));
  }

  @Test(expected = Test.None.class)
  public void testValidateCloudConfigWithClientSuccess() {
    YbcClient mockYbcClient = mock(YbcClient.class);
    CloudStoreConfig csConfig = CloudStoreConfig.getDefaultInstance();
    BackupServiceValidateCloudConfigResponse vcsResponse =
        BackupServiceValidateCloudConfigResponse.newBuilder()
            .setStatus(RpcControllerStatus.newBuilder().setCode(ControllerStatus.OK).build())
            .build();
    when(mockYbcClient.backupServiceValidateCloudConfig(
            any(BackupServiceValidateCloudConfigRequest.class)))
        .thenReturn(vcsResponse);
    spyYbcManager.validateCloudConfigWithClient("127.0.0.1", mockYbcClient, csConfig);
  }

  @Test
  public void testValidateCloudConfigWithClientWrongStatusFailure() {
    YbcClient mockYbcClient = mock(YbcClient.class);
    CloudStoreConfig csConfig = CloudStoreConfig.getDefaultInstance();
    BackupServiceValidateCloudConfigResponse vcsResponse =
        BackupServiceValidateCloudConfigResponse.newBuilder()
            .setStatus(
                RpcControllerStatus.newBuilder().setCode(ControllerStatus.CONNECTION_ERROR).build())
            .build();
    when(mockYbcClient.backupServiceValidateCloudConfig(
            any(BackupServiceValidateCloudConfigRequest.class)))
        .thenReturn(vcsResponse);
    RuntimeException ex =
        assertThrows(
            RuntimeException.class,
            () ->
                spyYbcManager.validateCloudConfigWithClient("127.0.0.1", mockYbcClient, csConfig));
    assertTrue(ex.getMessage().contains("CONNECTION_ERROR"));
  }

  @Test
  @Parameters({"2", "2.5"})
  public void testGetNumCoresCustomResourcesK8s(double cores) {
    Universe u =
        ModelFactory.createK8sUniverseCustomCores(
            "TEST-K8s", UUID.randomUUID(), testCustomer.getId(), null, null, true, cores);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.usek8sCustomResources))).thenReturn(true);
    int hardwareConcurrency =
        spyYbcManager.getCoreCountForTserver(u, u.getTServersInPrimaryCluster().get(0));
    assertEquals(Math.ceil(cores), hardwareConcurrency, 0.00);
  }

  @Test
  public void testPopulateControllerFlags() {
    Universe u =
        ModelFactory.createK8sUniverseCustomCores(
            "TEST-K8s", UUID.randomUUID(), testCustomer.getId(), null, null, true, 2.5);
    ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 3);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.usek8sCustomResources))).thenReturn(true);
    YbcClient mockYbcClient = mock(YbcClient.class);
    doReturn(mockYbcClient)
        .when(spyYbcManager)
        .getYbcClient(anyList(), anyInt(), nullable(String.class));
    // Initial flags
    NonRestartSettableControllerFlags.Builder ybcFlagsBuilder =
        NonRestartSettableControllerFlags.newBuilder();
    ybcFlagsBuilder.getDiskFlagsBuilder().setDiskReadBytesPerSec(12345678l);
    ybcFlagsBuilder.getDiskFlagsBuilder().setDiskWriteBytesPerSec(12345678l);
    ybcFlagsBuilder.getThrottleParamsBuilder().setMaxConcurrentUploads(5);
    NonRestartSettableControllerFlags ybcFlags = ybcFlagsBuilder.build();
    doReturn(ybcFlags).when(spyYbcManager).getControllerFlags(any(YbcClient.class));

    // Params to change
    Map<String, Long> paramsToModify = new HashMap<>();
    paramsToModify.put(GFlagsUtil.YBC_DISK_READ_BYTES_PER_SECOND, 123456789l);
    paramsToModify.put(GFlagsUtil.YBC_PER_UPLOAD_OBJECTS, 3l);
    // these are param map defaults and should not lead to change in values
    paramsToModify.put(GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS, 0l);
    paramsToModify.put(GFlagsUtil.YBC_DISK_WRITE_BYTES_PER_SECOND, -1l);

    NonRestartSettableControllerFlags.Builder paramBuilder =
        NonRestartSettableControllerFlags.newBuilder();
    Cluster c = u.getUniverseDetails().getPrimaryCluster();
    List<NodeDetails> tsNodes =
        u.getNodesInCluster(c.uuid).stream()
            .filter(nD -> nD.isTserver)
            .collect(Collectors.toList());
    spyYbcManager.populateControllerThrottleParamsMap(
        u, tsNodes, new HashMap<>(), paramBuilder, paramsToModify, false /* resetToDefaults */);
    // Assert new values
    assertEquals(123456789l, paramBuilder.getDiskFlags().getDiskReadBytesPerSec());
    assertEquals(5, paramBuilder.getThrottleParams().getMaxConcurrentUploads());
    // Assert old values intact for unchanged throttle params
    assertEquals(12345678l, paramBuilder.getDiskFlags().getDiskWriteBytesPerSec());
    assertEquals(3, paramBuilder.getThrottleParams().getPerUploadNumObjects());
  }

  @Test
  public void testPopulateControllerFlagsReset() {
    Universe u =
        ModelFactory.createK8sUniverseCustomCores(
            "TEST-K8s", UUID.randomUUID(), testCustomer.getId(), null, null, true, 2.5);
    ModelFactory.addNodesToUniverse(u.getUniverseUUID(), 3);

    long diskReadBytesDefault = 100000l;
    long diskWriteBytesDefault = 100000l;
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.usek8sCustomResources))).thenReturn(true);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.defaultDiskIoReadBytesPerSecond)))
        .thenReturn(diskReadBytesDefault);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.defaultDiskIoWriteBytesPerSecond)))
        .thenReturn(diskWriteBytesDefault);
    YbcClient mockYbcClient = mock(YbcClient.class);
    doReturn(mockYbcClient)
        .when(spyYbcManager)
        .getYbcClient(anyList(), anyInt(), nullable(String.class));
    // Initial flags
    NonRestartSettableControllerFlags.Builder ybcFlagsBuilder =
        NonRestartSettableControllerFlags.newBuilder();
    ybcFlagsBuilder.getDiskFlagsBuilder().setDiskReadBytesPerSec(12345678l);
    ybcFlagsBuilder.getDiskFlagsBuilder().setDiskWriteBytesPerSec(12345678l);
    ybcFlagsBuilder.getThrottleParamsBuilder().setMaxConcurrentUploads(5);
    // Setting to validate this does not change on reset to defaults
    ybcFlagsBuilder.getRetryFlagsBuilder().setMaxRetries(20);
    NonRestartSettableControllerFlags ybcFlags = ybcFlagsBuilder.build();
    doReturn(ybcFlags).when(spyYbcManager).getControllerFlags(any(YbcClient.class));

    // Params to change
    Map<String, Long> paramsToModify = new YbcThrottleParameters().getThrottleFlagsMap();

    NonRestartSettableControllerFlags.Builder paramBuilder =
        NonRestartSettableControllerFlags.newBuilder();
    Cluster c = u.getUniverseDetails().getPrimaryCluster();
    List<NodeDetails> tsNodes =
        u.getNodesInCluster(c.uuid).stream()
            .filter(nD -> nD.isTserver)
            .collect(Collectors.toList());
    spyYbcManager.populateControllerThrottleParamsMap(
        u, tsNodes, new HashMap<>(), paramBuilder, paramsToModify, true /* resetToDefaults */);
    // Assert new values in builder
    assertEquals(diskReadBytesDefault, paramBuilder.getDiskFlags().getDiskReadBytesPerSec());
    assertEquals(diskWriteBytesDefault, paramBuilder.getDiskFlags().getDiskWriteBytesPerSec());
    assertEquals(0, paramBuilder.getThrottleParams().getPerUploadNumObjects());
    assertEquals(0, paramBuilder.getThrottleParams().getPerDownloadNumObjects());
    assertEquals(0, paramBuilder.getThrottleParams().getMaxConcurrentUploads());
    assertEquals(0, paramBuilder.getThrottleParams().getMaxConcurrentDownloads());
    // Assert other settings did not change
    assertEquals(20, paramBuilder.getRetryFlags().getMaxRetries());
  }
}
