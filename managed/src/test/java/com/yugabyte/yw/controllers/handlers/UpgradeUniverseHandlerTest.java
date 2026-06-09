// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.mockStatic;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.GFlagDetails;
import com.yugabyte.yw.common.gflags.GFlagDiffEntry;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.CanaryUpgradeConfig;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeWithGFlags;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.MockedStatic;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class UpgradeUniverseHandlerTest extends FakeDBApplication {
  private UpgradeUniverseHandler handler;
  private GFlagsAuditHandler gFlagsAuditHandler;
  private GFlagsValidationHandler gFlagsValidationHandler;
  private Commissioner mockCommissioner;
  private RuntimeConfGetter runtimeConfGetter;
  private RuntimeConfigFactory mockRuntimeConfigFactory;
  private CertificateHelper mockCertificateHelper;

  @Before
  public void setUp() {
    mockCommissioner = mock(Commissioner.class);
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
        .thenReturn(UUID.randomUUID());
    gFlagsValidationHandler = mock(GFlagsValidationHandler.class);
    gFlagsAuditHandler = new GFlagsAuditHandler(gFlagsValidationHandler);
    runtimeConfGetter = mock(RuntimeConfGetter.class);
    mockRuntimeConfigFactory = mock(RuntimeConfigFactory.class);
    mockCertificateHelper = mock(CertificateHelper.class);

    // Mock KubernetesManagerFactory
    KubernetesManagerFactory mockKubernetesManagerFactory = mock(KubernetesManagerFactory.class);
    KubernetesManager mockKubernetesManager = mock(KubernetesManager.class);
    when(mockKubernetesManagerFactory.getManager()).thenReturn(mockKubernetesManager);
    when(mockKubernetesManager.getHelmPackagePath(anyString())).thenReturn("/tmp/helm/path");

    handler =
        new UpgradeUniverseHandler(
            mockCommissioner,
            mockKubernetesManagerFactory,
            mockRuntimeConfigFactory,
            mock(YbcManager.class),
            runtimeConfGetter,
            mockCertificateHelper,
            mock(AutoFlagUtil.class),
            mock(XClusterUniverseService.class),
            mock(TelemetryProviderService.class),
            mock(SoftwareUpgradeHelper.class));

    lenient()
        .when(
            runtimeConfGetter.getConfForScope(
                any(Universe.class), eq(UniverseConfKeys.enableCanaryUpgrade)))
        .thenReturn(true);
  }

  /** Minimal child row so canary resume can find a max-success position to prune past. */
  private static void persistSuccessfulCanaryChildSubtask(UUID parentTaskUuid) {
    TaskInfo subTask = new TaskInfo(TaskType.WaitForDuration, UUID.randomUUID());
    subTask.setParentUuid(parentTaskUuid);
    subTask.setPosition(0);
    subTask.setTaskState(TaskInfo.State.Success);
    subTask.setTaskParams(Json.newObject());
    subTask.setOwner("test");
    subTask.save();
  }

  private static Object[] tlsToggleCustomTypeNameParams() {
    return new Object[][] {
      {false, false, true, false, "TLS Toggle ON"},
      {false, false, false, true, "TLS Toggle ON"},
      {false, false, true, true, "TLS Toggle ON"},
      {true, false, true, true, "TLS Toggle ON"},
      {false, true, true, true, "TLS Toggle ON"},
      {true, true, false, true, "TLS Toggle OFF"},
      {true, true, true, false, "TLS Toggle OFF"},
      {true, true, false, false, "TLS Toggle OFF"},
      {false, true, false, false, "TLS Toggle OFF"},
      {true, false, false, false, "TLS Toggle OFF"},
      {false, true, true, false, "TLS Toggle Client ON Node OFF"},
      {true, false, false, true, "TLS Toggle Client OFF Node ON"}
    };
  }

  @Test
  @Parameters(method = "tlsToggleCustomTypeNameParams")
  public void testTLSToggleCustomTypeName(
      boolean clientBefore,
      boolean nodeBefore,
      boolean clientAfter,
      boolean nodeAfter,
      String expected) {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.enableClientToNodeEncrypt = clientBefore;
    userIntent.enableNodeToNodeEncrypt = nodeBefore;
    TlsToggleParams requestParams = new TlsToggleParams();
    requestParams.enableClientToNodeEncrypt = clientAfter;
    requestParams.enableNodeToNodeEncrypt = nodeAfter;

    String result = UpgradeUniverseHandler.generateTypeName(userIntent, requestParams);
    assertEquals(expected, result);
  }

  @Test
  public void testGenerateGFlagEntries() {
    Map<String, String> oldGFlags = new HashMap<>();
    Map<String, String> newGFlags = new HashMap<>();
    String serverType = ServerType.TSERVER.toString();
    String softwareVersion = "2.6";

    // removed gflag
    oldGFlags.put("stderrthreshold", "1");
    List<GFlagDiffEntry> gFlagDiffEntries =
        gFlagsAuditHandler.generateGFlagEntries(oldGFlags, newGFlags, serverType, softwareVersion);
    assertEquals(1, gFlagDiffEntries.size());
    assertEquals("stderrthreshold", gFlagDiffEntries.get(0).name);
    assertEquals("1", gFlagDiffEntries.get(0).oldValue);
    assertEquals(null, gFlagDiffEntries.get(0).newValue);

    // added gflag
    oldGFlags.clear();
    newGFlags.clear();
    newGFlags.put("minloglevel", "2");
    gFlagDiffEntries =
        gFlagsAuditHandler.generateGFlagEntries(oldGFlags, newGFlags, serverType, softwareVersion);
    assertEquals("minloglevel", gFlagDiffEntries.get(0).name);
    assertEquals(null, gFlagDiffEntries.get(0).oldValue);
    assertEquals("2", gFlagDiffEntries.get(0).newValue);

    // updated gflag
    oldGFlags.clear();
    newGFlags.clear();
    oldGFlags.put("max_log_size", "0");
    newGFlags.put("max_log_size", "1000");
    gFlagDiffEntries =
        gFlagsAuditHandler.generateGFlagEntries(oldGFlags, newGFlags, serverType, softwareVersion);
    assertEquals("max_log_size", gFlagDiffEntries.get(0).name);
    assertEquals("0", gFlagDiffEntries.get(0).oldValue);
    assertEquals("1000", gFlagDiffEntries.get(0).newValue);

    // unchanged gflag
    oldGFlags.clear();
    newGFlags.clear();
    oldGFlags.put("max_log_size", "2000");
    newGFlags.put("max_log_size", "2000");
    gFlagDiffEntries =
        gFlagsAuditHandler.generateGFlagEntries(oldGFlags, newGFlags, serverType, softwareVersion);
    assertEquals(0, gFlagDiffEntries.size());
  }

  @Test
  public void testConstructGFlagAuditPayload() throws IOException {
    initGflagDefaults();
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.getPrimaryCluster().userIntent.specificGFlags =
        SpecificGFlags.construct(ImmutableMap.of("master", "1"), ImmutableMap.of("tserver", "2"));
    JsonNode payload = gFlagsAuditHandler.constructGFlagAuditPayload(params);
    ObjectNode expected = Json.newObject();
    expected.set(
        "gflags",
        constructExpected(
            Collections.singletonList(createDiff("master", "1", null)),
            Collections.singletonList(createDiff("tserver", "2", null))));
    assertEquals(expected, payload);
  }

  @Test
  public void testConstructGFlagAuditPayloadReadReplica() throws IOException {
    initGflagDefaults();
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    UniverseDefinitionTaskParams.UserIntent rrUserIntent =
        u.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    rrUserIntent.specificGFlags =
        SpecificGFlags.construct(ImmutableMap.of("master", "5"), ImmutableMap.of("tserver2", "2"));
    u =
        Universe.saveDetails(
            u.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithReadReplica(rrUserIntent, null));
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.getReadOnlyClusters().get(0).userIntent.specificGFlags =
        SpecificGFlags.construct(ImmutableMap.of("master", "1"), ImmutableMap.of("tserver", "2"));
    JsonNode payload = gFlagsAuditHandler.constructGFlagAuditPayload(params);
    ObjectNode expected = Json.newObject();
    expected.set(
        "readonly_cluster_gflags",
        constructExpected(
            Collections.singletonList(createDiff("master", "1", "5")),
            Arrays.asList(createDiff("tserver2", null, "2"), createDiff("tserver", "2", null))));
    assertEquals(expected, payload);
  }

  @Test
  public void testConstructGFlagAuditPayloadBothChanged() throws IOException {
    initGflagDefaults();
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              details.getPrimaryCluster().userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      ImmutableMap.of("master", "1"), ImmutableMap.of("tserver", "1"));
              universe.setUniverseDetails(details);
            });
    UniverseDefinitionTaskParams.UserIntent rrUserIntent =
        u.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    rrUserIntent.specificGFlags =
        SpecificGFlags.construct(ImmutableMap.of("master", "5"), ImmutableMap.of("tserver2", "2"));
    u =
        Universe.saveDetails(
            u.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithReadReplica(rrUserIntent, null));
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.getPrimaryCluster().userIntent.specificGFlags =
        SpecificGFlags.construct(ImmutableMap.of("master", "2"), ImmutableMap.of("tserver", "3"));
    params.getReadOnlyClusters().get(0).userIntent.specificGFlags =
        SpecificGFlags.construct(ImmutableMap.of("master", "1"), ImmutableMap.of("tserver", "2"));
    JsonNode payload = gFlagsAuditHandler.constructGFlagAuditPayload(params);
    ObjectNode expected = Json.newObject();
    expected.set(
        "gflags",
        constructExpected(
            Collections.singletonList(createDiff("master", "2", "1")),
            Collections.singletonList(createDiff("tserver", "3", "1"))));
    expected.set(
        "readonly_cluster_gflags",
        constructExpected(
            Collections.singletonList(createDiff("master", "1", "5")),
            Arrays.asList(createDiff("tserver2", null, "2"), createDiff("tserver", "2", null))));
    assertEquals(expected, payload);
  }

  @Test
  public void testConstructGFlagAuditPayloadReadReplicaInherited() throws IOException {
    initGflagDefaults();
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    UniverseDefinitionTaskParams.UserIntent rrUserIntent =
        u.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    rrUserIntent.specificGFlags = SpecificGFlags.constructInherited();
    u =
        Universe.saveDetails(
            u.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithReadReplica(rrUserIntent, null));
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.getPrimaryCluster().userIntent.specificGFlags =
        SpecificGFlags.construct(ImmutableMap.of("master", "1"), ImmutableMap.of("tserver", "2"));
    JsonNode payload = gFlagsAuditHandler.constructGFlagAuditPayload(params);
    ObjectNode expected = Json.newObject();
    // Expecting no read replica gflags as long as they are inherited.
    expected.set(
        "gflags",
        constructExpected(
            Collections.singletonList(createDiff("master", "1", null)),
            Collections.singletonList(createDiff("tserver", "2", null))));
    assertEquals(expected, payload);
  }

  @Test
  public void testUpgradeGFlagsOldSchema() throws IOException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.GFlagsUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    when(runtimeConfGetter.getConfForScope(any(Customer.class), any())).thenReturn(false);
    initGflagDefaults();
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  details.getPrimaryCluster().userIntent;
              userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      ImmutableMap.of("master", "1"), ImmutableMap.of("tserver", "1"));
              userIntent.masterGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER);
              userIntent.tserverGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER);
              universe.setUniverseDetails(details);
            });
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    Map<String, String> masterGFlags = ImmutableMap.of("asd", "10");
    Map<String, String> tserverGFlags = ImmutableMap.of("awesd", "15");
    params.masterGFlags = masterGFlags;
    params.tserverGFlags = tserverGFlags;
    handler.upgradeGFlags(params, c, Universe.getOrBadRequest(u.getUniverseUUID()));
    ArgumentCaptor<UpgradeTaskParams> paramsArgumentCaptor =
        ArgumentCaptor.forClass(UpgradeTaskParams.class);
    verify(mockCommissioner).submit(any(), paramsArgumentCaptor.capture());
    GFlagsUpgradeParams newParams = (GFlagsUpgradeParams) paramsArgumentCaptor.getValue();
    // Verifying that specificGFlags field is initialized.
    assertEquals(
        SpecificGFlags.construct(masterGFlags, tserverGFlags),
        newParams.getPrimaryCluster().userIntent.specificGFlags);
  }

  @Test
  public void testUpgradeGFlagsOldSchemaWithRRInherited() throws IOException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.GFlagsUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    when(runtimeConfGetter.getConfForScope(any(Customer.class), any())).thenReturn(false);
    initGflagDefaults();
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  details.getPrimaryCluster().userIntent;
              userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      ImmutableMap.of("master", "1"), ImmutableMap.of("tserver", "1"));
              userIntent.masterGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER);
              userIntent.tserverGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER);
              universe.setUniverseDetails(details);
            });
    UniverseDefinitionTaskParams.UserIntent rrUserIntent =
        u.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    rrUserIntent.specificGFlags = SpecificGFlags.constructInherited();
    u =
        Universe.saveDetails(
            u.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithReadReplica(rrUserIntent, null));
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    Map<String, String> masterGFlags = ImmutableMap.of("asd", "10");
    Map<String, String> tserverGFlags = ImmutableMap.of("awesd", "15");
    params.masterGFlags = masterGFlags;
    params.tserverGFlags = tserverGFlags;
    handler.upgradeGFlags(params, c, Universe.getOrBadRequest(u.getUniverseUUID()));
    ArgumentCaptor<UpgradeTaskParams> paramsArgumentCaptor =
        ArgumentCaptor.forClass(UpgradeTaskParams.class);
    verify(mockCommissioner).submit(any(), paramsArgumentCaptor.capture());
    GFlagsUpgradeParams newParams = (GFlagsUpgradeParams) paramsArgumentCaptor.getValue();
    // Verifying that specificGFlags field is initialized.
    assertEquals(
        SpecificGFlags.construct(masterGFlags, tserverGFlags),
        newParams.getPrimaryCluster().userIntent.specificGFlags);
  }

  @Test
  public void testUpgradeGFlagsOldSchemaWithRRNonInherited() throws IOException {
    when(runtimeConfGetter.getConfForScope(any(Customer.class), any())).thenReturn(false);
    initGflagDefaults();
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  details.getPrimaryCluster().userIntent;
              userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      ImmutableMap.of("master", "1"), ImmutableMap.of("tserver", "1"));
              userIntent.masterGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER);
              userIntent.tserverGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER);
              universe.setUniverseDetails(details);
            });
    UniverseDefinitionTaskParams.UserIntent rrUserIntent =
        u.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    rrUserIntent.specificGFlags =
        SpecificGFlags.construct(ImmutableMap.of("wer", "3"), ImmutableMap.of("ere", "5"));
    u =
        Universe.saveDetails(
            u.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithReadReplica(rrUserIntent, null));
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    Map<String, String> masterGFlags = ImmutableMap.of("asd", "10");
    Map<String, String> tserverGFlags = ImmutableMap.of("awesd", "15");
    params.masterGFlags = masterGFlags;
    params.tserverGFlags = tserverGFlags;
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.upgradeGFlags(
                    params, c, Universe.getOrBadRequest(params.getUniverseUUID())));
    assertEquals(
        "Cannot upgrade gflags using old fields because read replica has overriden gflags."
            + " Please modify specificGFlags to do upgrade.",
        exception.getMessage());
  }

  @Test
  public void testUpgradeGFlagsOldSchemaWithRROverridenPerAZ() throws IOException {
    when(runtimeConfGetter.getConfForScope(any(Customer.class), any())).thenReturn(false);
    initGflagDefaults();
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  details.getPrimaryCluster().userIntent;
              userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      ImmutableMap.of("master", "1"), ImmutableMap.of("tserver", "1"));
              userIntent.masterGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER);
              userIntent.tserverGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER);
              universe.setUniverseDetails(details);
            });
    UniverseDefinitionTaskParams.UserIntent rrUserIntent =
        u.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    rrUserIntent.specificGFlags =
        SpecificGFlags.construct(ImmutableMap.of("wer", "3"), ImmutableMap.of("ere", "5"));
    rrUserIntent.specificGFlags.setPerAZ(
        ImmutableMap.of(
            UUID.randomUUID(),
            SpecificGFlags.construct(ImmutableMap.of("a", "b"), ImmutableMap.of("a", "b"))
                .getPerProcessFlags()));
    u =
        Universe.saveDetails(
            u.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithReadReplica(rrUserIntent, null));
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    Map<String, String> masterGFlags = ImmutableMap.of("asd", "10");
    Map<String, String> tserverGFlags = ImmutableMap.of("awesd", "15");
    params.masterGFlags = masterGFlags;
    params.tserverGFlags = tserverGFlags;
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.upgradeGFlags(
                    params, c, Universe.getOrBadRequest(params.getUniverseUUID())));
    assertEquals(
        "Cannot upgrade gflags using old fields because there are overrides per az"
            + " in readonly cluster. Please modify specificGFlags to do upgrade.",
        exception.getMessage());
  }

  @Test
  public void testUpgradeGFlagsOldSchemaWithOverridenPerAZ() throws IOException {
    when(runtimeConfGetter.getConfForScope(any(Customer.class), any())).thenReturn(false);
    initGflagDefaults();
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  details.getPrimaryCluster().userIntent;
              userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      ImmutableMap.of("master", "1"), ImmutableMap.of("tserver", "1"));
              userIntent.specificGFlags.setPerAZ(
                  ImmutableMap.of(
                      UUID.randomUUID(),
                      SpecificGFlags.construct(ImmutableMap.of("a", "b"), ImmutableMap.of("a", "b"))
                          .getPerProcessFlags()));
              userIntent.masterGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER);
              userIntent.tserverGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER);
              universe.setUniverseDetails(details);
            });

    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.masterGFlags = ImmutableMap.of("asd", "10");
    params.tserverGFlags = ImmutableMap.of("awesd", "15");
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.upgradeGFlags(
                    params, c, Universe.getOrBadRequest(params.getUniverseUUID())));
    assertEquals(
        "Cannot upgrade gflags using old fields because there are overrides per az"
            + " in primary cluster. Please modify specificGFlags to do upgrade.",
        exception.getMessage());
  }

  @Test
  public void testUpgradeGFlagsNoOP() throws IOException {
    when(runtimeConfGetter.getConfForScope(any(Customer.class), any())).thenReturn(false);
    initGflagDefaults();
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  details.getPrimaryCluster().userIntent;
              userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      ImmutableMap.of("master", "1"), ImmutableMap.of("tserver", "1"));
              userIntent.masterGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER);
              userIntent.tserverGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER);
              universe.setUniverseDetails(details);
            });
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.upgradeGFlags(
                    params, c, Universe.getOrBadRequest(params.getUniverseUUID())));
    assertEquals(UpgradeWithGFlags.SPECIFIC_GFLAGS_NO_CHANGES_ERROR, exception.getMessage());
  }

  @Test
  public void testUpgradeGFlagsNoOPSameFlags() throws IOException {
    when(runtimeConfGetter.getConfForScope(any(Customer.class), any())).thenReturn(false);
    initGflagDefaults();
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  details.getPrimaryCluster().userIntent;
              userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      ImmutableMap.of("master", "1"), ImmutableMap.of("tserver", "1"));
              userIntent.masterGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER);
              userIntent.tserverGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER);
              universe.setUniverseDetails(details);
            });
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.tserverGFlags =
        GFlagsUtil.getBaseGFlags(
            ServerType.TSERVER,
            u.getUniverseDetails().getPrimaryCluster(),
            u.getUniverseDetails().clusters);
    params.masterGFlags =
        GFlagsUtil.getBaseGFlags(
            ServerType.MASTER,
            u.getUniverseDetails().getPrimaryCluster(),
            u.getUniverseDetails().clusters);
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.upgradeGFlags(
                    params, c, Universe.getOrBadRequest(params.getUniverseUUID())));
    assertEquals(UpgradeWithGFlags.SPECIFIC_GFLAGS_NO_CHANGES_ERROR, exception.getMessage());
  }

  @Test
  public void testUpgradeGFlagsOldSchemaK8sOperator() throws IOException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.GFlagsUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    when(runtimeConfGetter.getConfForScope(any(Customer.class), any())).thenReturn(false);
    initGflagDefaults();
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  details.getPrimaryCluster().userIntent;
              userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      ImmutableMap.of("master", "1"), ImmutableMap.of("tserver", "1"));
              userIntent.masterGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER);
              userIntent.tserverGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER);
              universe.setUniverseDetails(details);
            });
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    Map<String, String> masterGFlags = ImmutableMap.of("asd", "10");
    Map<String, String> tserverGFlags = ImmutableMap.of("awesd", "15");
    params.masterGFlags = masterGFlags;
    params.tserverGFlags = tserverGFlags;
    // Erasing specificGFlags in
    params.getPrimaryCluster().userIntent.specificGFlags = null;
    handler.upgradeGFlags(params, c, Universe.getOrBadRequest(u.getUniverseUUID()));
    ArgumentCaptor<UpgradeTaskParams> paramsArgumentCaptor =
        ArgumentCaptor.forClass(UpgradeTaskParams.class);
    verify(mockCommissioner).submit(any(), paramsArgumentCaptor.capture());
    GFlagsUpgradeParams newParams = (GFlagsUpgradeParams) paramsArgumentCaptor.getValue();
    // Verifying that specificGFlags field is initialized.
    assertEquals(
        SpecificGFlags.construct(masterGFlags, tserverGFlags),
        newParams.getPrimaryCluster().userIntent.specificGFlags);
  }

  private void initGflagDefaults() throws IOException {
    when(gFlagsValidationHandler.getGFlagsMetadata(anyString(), anyString(), anyString()))
        .thenAnswer(
            invocation -> {
              GFlagDetails result = new GFlagDetails();
              result.defaultValue = invocation.getArgument(2);
              result.name = invocation.getArgument(2);
              return result;
            });
  }

  private ObjectNode constructExpected(List<JsonNode> master, List<JsonNode> tserver) {
    ObjectNode res = Json.newObject();
    ArrayNode mastArray = Json.newArray();
    master.forEach(mastArray::add);
    ArrayNode tservArray = Json.newArray();
    tserver.forEach(tservArray::add);
    res.set("master", mastArray);
    res.set("tserver", tservArray);
    return res;
  }

  private JsonNode createDiff(String gflag, String value, String oldValue) {
    ObjectNode res = Json.newObject();
    res.put("name", gflag);
    res.put("old", oldValue);
    res.put("new", value);
    res.put("default", gflag);
    return res;
  }

  // Kubernetes Certificate Rotation Tests
  @Test
  public void testRotateCertsKubernetesRootCertRotation()
      throws IOException, NoSuchAlgorithmException {
    UUID fakeTaskUUID =
        FakeDBApplication.buildTaskInfo(null, TaskType.CertsRotateKubernetesUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Customer c = ModelFactory.testCustomer();
    Universe u = createKubernetesUniverse(c);
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              details.getPrimaryCluster().userIntent.enableClientToNodeEncrypt = false;
              universe.setUniverseDetails(details);
            });

    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.rootCA = UUID.randomUUID();
    params.rootAndClientRootCASame = true;

    // Create certificate info
    String certBasePath = TestHelper.TMP_PATH + "/" + UUID.randomUUID();
    TestHelper.createTempFile(certBasePath, "test.crt", "test-cert");
    CertificateInfo.create(
        params.rootCA,
        c.getUuid(),
        "test-cert",
        new Date(),
        new Date(),
        "privateKey",
        certBasePath + "/test.crt",
        CertConfigType.SelfSigned);

    handler.rotateCerts(params, c, u);

    ArgumentCaptor<CertsRotateParams> paramsArgumentCaptor =
        ArgumentCaptor.forClass(CertsRotateParams.class);
    verify(mockCommissioner)
        .submit(eq(TaskType.CertsRotateKubernetesUpgrade), paramsArgumentCaptor.capture());

    CertsRotateParams capturedParams = paramsArgumentCaptor.getValue();
    assertEquals(CertsRotateParams.CertRotationType.RootCert, capturedParams.rootCARotationType);
  }

  @Test
  public void testRotateCertsKubernetesServerCertRotation()
      throws IOException, NoSuchAlgorithmException {
    UUID fakeTaskUUID =
        FakeDBApplication.buildTaskInfo(null, TaskType.CertsRotateKubernetesUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
        .thenReturn(fakeTaskUUID);

    // Mock the static method
    try (MockedStatic<CertificateHelper> mockedCertificateHelper =
        mockStatic(CertificateHelper.class)) {
      mockedCertificateHelper
          .when(() -> CertificateHelper.createClientCertificate(any(), any(), any()))
          .thenReturn(null);

      Customer c = ModelFactory.testCustomer();
      Universe u = createKubernetesUniverse(c);

      // Set up universe with existing rootCA
      UUID existingRootCA = UUID.randomUUID();
      u =
          Universe.saveDetails(
              u.getUniverseUUID(),
              universe -> {
                UniverseDefinitionTaskParams details = universe.getUniverseDetails();
                details.rootCA = existingRootCA;
                details.setClientRootCA(existingRootCA);
                details.getPrimaryCluster().userIntent.enableClientToNodeEncrypt = true;
                details.getPrimaryCluster().userIntent.enableNodeToNodeEncrypt = true;
                universe.setUniverseDetails(details);
              });

      CertsRotateParams params = new CertsRotateParams();
      params.setUniverseUUID(u.getUniverseUUID());
      params.clusters = u.getUniverseDetails().clusters;
      params.rootCA = existingRootCA; // Same rootCA
      params.setClientRootCA(existingRootCA);
      params.selfSignedServerCertRotate = true;
      params.selfSignedClientCertRotate = true;
      params.rootAndClientRootCASame = true;

      // Create certificate info
      String certBasePath = TestHelper.TMP_PATH + "/" + UUID.randomUUID();
      TestHelper.createTempFile(certBasePath, "test.crt", "test-cert");
      CertificateInfo.create(
          existingRootCA,
          c.getUuid(),
          "test-cert",
          new Date(),
          new Date(),
          "privateKey",
          certBasePath + "/test.crt",
          CertConfigType.SelfSigned);

      handler.rotateCerts(params, c, u);

      ArgumentCaptor<CertsRotateParams> paramsArgumentCaptor =
          ArgumentCaptor.forClass(CertsRotateParams.class);
      verify(mockCommissioner)
          .submit(eq(TaskType.CertsRotateKubernetesUpgrade), paramsArgumentCaptor.capture());

      CertsRotateParams capturedParams = paramsArgumentCaptor.getValue();
      assertEquals(
          CertsRotateParams.CertRotationType.ServerCert, capturedParams.rootCARotationType);
    } catch (Exception e) {
      fail();
    }
  }

  @Test
  public void testRotateCertsKubernetesPartialServerCertRotation()
      throws IOException, NoSuchAlgorithmException {
    Customer c = ModelFactory.testCustomer();
    Universe u = createKubernetesUniverse(c);

    // Set up universe with existing rootCA
    UUID existingRootCA = UUID.randomUUID();
    Universe updatedUniverse =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              details.rootCA = existingRootCA;
              details.setClientRootCA(existingRootCA);
              details.getPrimaryCluster().userIntent.enableClientToNodeEncrypt = true;
              details.getPrimaryCluster().userIntent.enableNodeToNodeEncrypt = true;
              universe.setUniverseDetails(details);
            });

    // Create certificate info
    String certBasePath = TestHelper.TMP_PATH + "/" + UUID.randomUUID();
    TestHelper.createTempFile(certBasePath, "test.crt", "test-cert");
    CertificateInfo.create(
        existingRootCA,
        c.getUuid(),
        "test-cert",
        new Date(),
        new Date(),
        "privateKey",
        certBasePath + "/test.crt",
        CertConfigType.SelfSigned);

    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.selfSignedServerCertRotate = true;
    params.selfSignedClientCertRotate = false; // Only server cert rotation
    params.rootAndClientRootCASame = true;
    params.rootCA = existingRootCA;
    params.setClientRootCA(existingRootCA);

    // Only server cert rotation
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class, () -> handler.rotateCerts(params, c, updatedUniverse));

    assertEquals(
        "Cannot rotate only node to node certificate when client to node encryption is enabled.",
        exception.getMessage());

    params.selfSignedClientCertRotate = true; // Only client cert rotation
    params.selfSignedServerCertRotate = false;

    // Only client cert rotation
    exception =
        assertThrows(
            PlatformServiceException.class, () -> handler.rotateCerts(params, c, updatedUniverse));
    assertEquals(
        "Cannot rotate only client to node certificate when node to node encryption is enabled.",
        exception.getMessage());

    params.selfSignedClientCertRotate = true;
    params.selfSignedServerCertRotate = true;

    // Mock the static method
    try (MockedStatic<CertificateHelper> mockedCertificateHelper =
        mockStatic(CertificateHelper.class)) {
      mockedCertificateHelper
          .when(() -> CertificateHelper.createClientCertificate(any(), any(), any()))
          .thenReturn(null);

      UUID fakeTaskUUID =
          FakeDBApplication.buildTaskInfo(null, TaskType.CertsRotateKubernetesUpgrade);
      when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
          .thenReturn(fakeTaskUUID);

      // Rotate both server and client cert rotation
      handler.rotateCerts(params, c, updatedUniverse);
    }
  }

  @Test
  public void testRotateCertsKubernetesNoRotationRequested()
      throws IOException, NoSuchAlgorithmException {

    Customer c = ModelFactory.testCustomer();
    Universe u = createKubernetesUniverse(c);

    // Set up universe with existing rootCA
    UUID existingRootCA = UUID.randomUUID();
    Universe updatedUniverse =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              details.rootCA = existingRootCA;
              details.getPrimaryCluster().userIntent.enableClientToNodeEncrypt = false;
              universe.setUniverseDetails(details);
            });

    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.rootAndClientRootCASame = true;
    params.rootCA = existingRootCA;
    // No rotation flags set

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class, () -> handler.rotateCerts(params, c, updatedUniverse));

    assertEquals(
        "No changes in rootCA or server certificate rotation has been requested.",
        exception.getMessage());
  }

  @Test
  public void testRotateCertsKubernetesDifferentClientRootCA()
      throws IOException, NoSuchAlgorithmException {
    Customer c = ModelFactory.testCustomer();
    Universe u = createKubernetesUniverse(c);

    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.rootCA = UUID.randomUUID();
    params.setClientRootCA(UUID.randomUUID()); // Different from rootCA
    params.rootAndClientRootCASame = true;

    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> handler.rotateCerts(params, c, u));

    assertEquals(
        "rootCA and clientRootCA cannot be different for Kubernetes certificate rotation.",
        exception.getMessage());
  }

  @Test
  public void testRotateCertsKubernetesRootAndClientRootCASameFalse()
      throws IOException, NoSuchAlgorithmException {
    Customer c = ModelFactory.testCustomer();
    Universe u = createKubernetesUniverse(c);

    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.rootCA = UUID.randomUUID();
    params.rootAndClientRootCASame = false;

    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> handler.rotateCerts(params, c, u));

    assertEquals(
        "rootAndClientRootCASame cannot be false for Kubernetes universes.",
        exception.getMessage());
  }

  @Test
  public void testRotateCertsKubernetesUnsupportedCertType()
      throws IOException, NoSuchAlgorithmException {
    Customer c = ModelFactory.testCustomer();
    Universe u = createKubernetesUniverse(c);

    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.rootCA = UUID.randomUUID();
    params.rootAndClientRootCASame = true;
    String certBasePath = TestHelper.TMP_PATH + "/" + UUID.randomUUID();
    TestHelper.createTempFile(certBasePath, "test.crt", "test-cert");

    // Create certificate info with unsupported type
    CertificateInfo.create(
        params.rootCA,
        c.getUuid(),
        "test-cert",
        new Date(),
        new Date(),
        "privateKey",
        certBasePath + "/test.crt",
        CertConfigType.CustomCertHostPath);

    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> handler.rotateCerts(params, c, u));

    assertEquals(
        "CustomCertHostPath certificates are not supported for Kubernetes certificate rotation."
            + " Use CertManager instead.",
        exception.getMessage());
  }

  @Test
  public void testRotateCertsEncryptionDisabledWithCA() {
    Customer c = ModelFactory.testCustomer();
    Universe u = createKubernetesUniverseInternal(c, null, false, false);

    CertsRotateParams params = new CertsRotateParams();
    params.rootCA = UUID.randomUUID();

    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> params.verifyParams(u, true));

    assertEquals(
        "Cannot rotate rootCA or clientRootCA when encryption-in-transit is disabled.",
        exception.getMessage());

    params.rootCA = null;
    params.setClientRootCA(UUID.randomUUID());
    params.rootAndClientRootCASame = false;

    exception = assertThrows(PlatformServiceException.class, () -> params.verifyParams(u, true));

    assertEquals(
        "Cannot rotate rootCA or clientRootCA when encryption-in-transit is disabled.",
        exception.getMessage());
  }

  @Test
  public void testRotateClientCertsEncryptionDisabled() {
    Customer c = ModelFactory.testCustomer();
    Universe u = createKubernetesUniverseInternal(c, UUID.randomUUID(), true, false);

    CertsRotateParams params = new CertsRotateParams();
    params.selfSignedClientCertRotate = true;
    params.selfSignedServerCertRotate = false;

    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> handler.rotateCerts(params, c, u));

    assertEquals(
        "Cannot rotate client certificate when client to node encryption is disabled.",
        exception.getMessage());
  }

  @Test
  public void testRotateServerCertsEncryptionDisabled() {
    Customer c = ModelFactory.testCustomer();
    Universe u = createKubernetesUniverseInternal(c, UUID.randomUUID(), false, true);

    CertsRotateParams params = new CertsRotateParams();
    params.selfSignedServerCertRotate = true;
    params.selfSignedClientCertRotate = false;

    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> handler.rotateCerts(params, c, u));

    assertEquals(
        "Cannot rotate server certificate when node to node encryption is disabled.",
        exception.getMessage());
  }

  @Test
  public void testRotateCertsCreateNewRootCASuccess() throws IOException, NoSuchAlgorithmException {
    UUID fakeTaskUUID =
        FakeDBApplication.buildTaskInfo(null, TaskType.CertsRotateKubernetesUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
        .thenReturn(fakeTaskUUID);

    UUID newRootCA = UUID.randomUUID();
    when(mockCertificateHelper.createRootCA(any(), anyString(), any(UUID.class)))
        .thenReturn(newRootCA);
    when(mockRuntimeConfigFactory.staticApplicationConf()).thenReturn(app.config());

    Customer c = ModelFactory.testCustomer();
    Universe u = createKubernetesUniverseInternal(c, null, true, false);

    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.rootCA = null;
    params.rootAndClientRootCASame = true;

    // Create certificate info for the new rootCA
    String certBasePath = TestHelper.TMP_PATH + "/" + UUID.randomUUID();
    TestHelper.createTempFile(certBasePath, "test.crt", "test-cert");
    CertificateInfo.create(
        newRootCA,
        c.getUuid(),
        "test-cert",
        new Date(),
        new Date(),
        "privateKey",
        certBasePath + "/test.crt",
        CertConfigType.SelfSigned);

    handler.rotateCerts(params, c, u);

    ArgumentCaptor<CertsRotateParams> paramsArgumentCaptor =
        ArgumentCaptor.forClass(CertsRotateParams.class);
    verify(mockCommissioner)
        .submit(eq(TaskType.CertsRotateKubernetesUpgrade), paramsArgumentCaptor.capture());

    CertsRotateParams capturedParams = paramsArgumentCaptor.getValue();
    assertEquals(newRootCA, capturedParams.rootCA);
    verify(mockCertificateHelper)
        .createRootCA(any(), eq(u.getUniverseDetails().nodePrefix), eq(c.getUuid()));
  }

  @Test
  public void testRotateCertsExistingRootCASuccess() throws IOException, NoSuchAlgorithmException {
    UUID fakeTaskUUID =
        FakeDBApplication.buildTaskInfo(null, TaskType.CertsRotateKubernetesUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
        .thenReturn(fakeTaskUUID);

    UUID newRootCA = UUID.randomUUID();
    when(mockCertificateHelper.createRootCA(any(), anyString(), any(UUID.class)))
        .thenReturn(newRootCA);
    when(mockRuntimeConfigFactory.staticApplicationConf()).thenReturn(app.config());

    Customer c = ModelFactory.testCustomer();
    Universe u = createKubernetesUniverseInternal(c, null, true, false);

    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.rootCA = newRootCA;
    params.rootAndClientRootCASame = true;

    // Create certificate info for the new rootCA
    String certBasePath = TestHelper.TMP_PATH + "/" + UUID.randomUUID();
    TestHelper.createTempFile(certBasePath, "test.crt", "test-cert");
    CertificateInfo.create(
        newRootCA,
        c.getUuid(),
        "test-cert",
        new Date(),
        new Date(),
        "privateKey",
        certBasePath + "/test.crt",
        CertConfigType.SelfSigned);

    handler.rotateCerts(params, c, u);

    ArgumentCaptor<CertsRotateParams> paramsArgumentCaptor =
        ArgumentCaptor.forClass(CertsRotateParams.class);
    verify(mockCommissioner)
        .submit(eq(TaskType.CertsRotateKubernetesUpgrade), paramsArgumentCaptor.capture());

    CertsRotateParams capturedParams = paramsArgumentCaptor.getValue();
    assertEquals(newRootCA, capturedParams.rootCA);
  }

  @Test
  public void testRotateCertsCreateNewClientRootCASuccess()
      throws IOException, NoSuchAlgorithmException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CertsRotate);
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
        .thenReturn(fakeTaskUUID);

    UUID existingRootCA = UUID.randomUUID();
    UUID newClientRootCA = UUID.randomUUID();
    when(mockCertificateHelper.createClientRootCA(any(), anyString(), any(UUID.class)))
        .thenReturn(newClientRootCA);
    when(mockRuntimeConfigFactory.staticApplicationConf()).thenReturn(app.config());

    Customer c = ModelFactory.testCustomer();
    // Use non-Kubernetes universe since rootAndClientRootCASame = false is only valid for non-K8s
    Universe u = ModelFactory.createUniverse(c.getId());
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              details.getPrimaryCluster().userIntent.providerType = CloudType.aws;
              details.getPrimaryCluster().userIntent.enableNodeToNodeEncrypt = true;
              details.getPrimaryCluster().userIntent.enableClientToNodeEncrypt = true;
              details.rootCA = existingRootCA;
              universe.setUniverseDetails(details);
            });

    // Create certificate info for existing rootCA
    String certBasePath = TestHelper.TMP_PATH + "/" + UUID.randomUUID();
    TestHelper.createTempFile(certBasePath, "test.crt", "test-cert");
    CertificateInfo.create(
        existingRootCA,
        c.getUuid(),
        "test-cert",
        new Date(),
        new Date(),
        "privateKey",
        certBasePath + "/test.crt",
        CertConfigType.SelfSigned);

    // Create certificate info for new clientRootCA
    TestHelper.createTempFile(certBasePath, "test-client.crt", "test-client-cert");
    CertificateInfo.create(
        newClientRootCA,
        c.getUuid(),
        "test-client-cert",
        new Date(),
        new Date(),
        "privateKey",
        certBasePath + "/test-client.crt",
        CertConfigType.SelfSigned);

    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.setClientRootCA(null);
    params.rootCA = existingRootCA;
    params.rootAndClientRootCASame = false;

    // Mock the static method for createClientCertificate
    try (MockedStatic<CertificateHelper> mockedCertificateHelper =
        mockStatic(CertificateHelper.class)) {
      mockedCertificateHelper
          .when(() -> CertificateHelper.createClientCertificate(any(), any(), any()))
          .thenReturn(null);

      handler.rotateCerts(params, c, u);

      ArgumentCaptor<CertsRotateParams> paramsArgumentCaptor =
          ArgumentCaptor.forClass(CertsRotateParams.class);
      verify(mockCommissioner).submit(eq(TaskType.CertsRotate), paramsArgumentCaptor.capture());

      CertsRotateParams capturedParams = paramsArgumentCaptor.getValue();
      assertEquals(newClientRootCA, capturedParams.getClientRootCA());
      verify(mockCertificateHelper)
          .createClientRootCA(any(), eq(u.getUniverseDetails().nodePrefix), eq(c.getUuid()));
    }
  }

  @Test
  public void testRotateCertsUseRootCAAsClientRootCAWhenRootAndClientRootCASame()
      throws IOException, NoSuchAlgorithmException {
    UUID fakeTaskUUID =
        FakeDBApplication.buildTaskInfo(null, TaskType.CertsRotateKubernetesUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
        .thenReturn(fakeTaskUUID);

    UUID existingRootCA = UUID.randomUUID();
    when(mockRuntimeConfigFactory.staticApplicationConf()).thenReturn(app.config());

    Customer c = ModelFactory.testCustomer();
    Universe u = createKubernetesUniverseInternal(c, existingRootCA, true, true);

    // Create certificate info for existing rootCA
    String certBasePath = TestHelper.TMP_PATH + "/" + UUID.randomUUID();
    TestHelper.createTempFile(certBasePath, "test.crt", "test-cert");
    CertificateInfo.create(
        existingRootCA,
        c.getUuid(),
        "test-cert",
        new Date(),
        new Date(),
        "privateKey",
        certBasePath + "/test.crt",
        CertConfigType.SelfSigned);

    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.rootCA = existingRootCA;
    params.setClientRootCA(null);
    params.rootAndClientRootCASame = true;

    // Mock the static method for createClientCertificate
    try (MockedStatic<CertificateHelper> mockedCertificateHelper =
        mockStatic(CertificateHelper.class)) {
      mockedCertificateHelper
          .when(() -> CertificateHelper.createClientCertificate(any(), any(), any()))
          .thenReturn(null);

      handler.rotateCerts(params, c, u);

      ArgumentCaptor<CertsRotateParams> paramsArgumentCaptor =
          ArgumentCaptor.forClass(CertsRotateParams.class);
      verify(mockCommissioner)
          .submit(eq(TaskType.CertsRotateKubernetesUpgrade), paramsArgumentCaptor.capture());

      CertsRotateParams capturedParams = paramsArgumentCaptor.getValue();
      assertEquals(existingRootCA, capturedParams.getClientRootCA());
    }
  }

  @Test
  public void testUpgradeDBVersionCanaryRejectedWhenFlagDisabled() {
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.ybSoftwareVersion = "2.21.0.0-b2";
    params.clusters.add(u.getUniverseDetails().getPrimaryCluster());
    params.canaryUpgradeConfig = new CanaryUpgradeConfig();
    params.canaryUpgradeConfig.pauseAfterMasters = true;

    when(runtimeConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.enableCanaryUpgrade)))
        .thenReturn(false);

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.upgradeDBVersion(params, c, Universe.getOrBadRequest(u.getUniverseUUID())));
    assertEquals(400, ex.getHttpStatus());
    assertTrue(ex.getMessage().contains("Canary upgrade is disabled"));
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testUpgradeDBVersionCanarySucceedsWhenFlagEnabled() {
    when(runtimeConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.enableYbcForUniverse)))
        .thenReturn(false);
    when(runtimeConfGetter.getGlobalConf(eq(GlobalConfKeys.ybcCompatibleDbVersion)))
        .thenReturn("2.17.0.0-b1");

    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion =
                  "2.20.2.0-b1";
            });
    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    // Stable -> stable upgrade (avoids preview/stable check); both >= YBDB rollback threshold.
    params.ybSoftwareVersion = "2.22.0.0-b1";
    params.clusters.add(u.getUniverseDetails().getPrimaryCluster());
    params.canaryUpgradeConfig = new CanaryUpgradeConfig();
    params.canaryUpgradeConfig.pauseAfterMasters = true;

    UUID fakeTaskUuid = FakeDBApplication.buildTaskInfo(null, TaskType.SoftwareUpgradeYB);
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
        .thenReturn(fakeTaskUuid);

    UUID result =
        handler.upgradeDBVersion(params, c, Universe.getOrBadRequest(u.getUniverseUUID()));

    assertEquals(fakeTaskUuid, result);
    verify(mockCommissioner)
        .submit(eq(TaskType.SoftwareUpgradeYB), any(SoftwareUpgradeParams.class));
  }

  @Test
  public void testUpgradeDBVersionCanaryRejectedWhenUniversePausedWithoutResume() {
    when(runtimeConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.enableYbcForUniverse)))
        .thenReturn(false);
    when(runtimeConfGetter.getGlobalConf(eq(GlobalConfKeys.ybcCompatibleDbVersion)))
        .thenReturn("2.17.0.0-b1");

    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    final UUID universeUuid = u.getUniverseUUID();
    UUID pausedTaskUuid = UUID.randomUUID();
    Universe.saveDetails(
        universeUuid,
        universe -> {
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion =
              "2.20.2.0-b1";
          universe.getUniverseDetails().softwareUpgradeState =
              UniverseDefinitionTaskParams.SoftwareUpgradeState.Paused;
          universe.getUniverseDetails().updatingTaskUUID = pausedTaskUuid;
          universe.setUniverseDetails(universe.getUniverseDetails());
        });

    Universe fresh = Universe.getOrBadRequest(universeUuid);
    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.setUniverseUUID(universeUuid);
    params.ybSoftwareVersion = "2.22.0.0-b1";
    params.clusters.add(fresh.getUniverseDetails().getPrimaryCluster());
    params.canaryUpgradeConfig = new CanaryUpgradeConfig();
    params.canaryUpgradeConfig.pauseAfterMasters = true;

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.upgradeDBVersion(params, c, Universe.getOrBadRequest(universeUuid)));
    assertEquals(400, ex.getHttpStatus());
    assertTrue(ex.getMessage().contains("paused canary"));
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testResumeCanarySoftwareUpgradeRejectsWhenFlagDisabled() {
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    UUID taskUUID = UUID.randomUUID();

    when(runtimeConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.enableCanaryUpgrade)))
        .thenReturn(false);

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.resumeCanarySoftwareUpgrade(c.getUuid(), u.getUniverseUUID(), taskUUID));
    assertEquals(400, e.getHttpStatus());
    assertTrue(e.getMessage().contains("Canary upgrade is disabled"));
    verify(mockCommissioner, never()).submit(any(), any(), any(UUID.class));
  }

  @Test
  public void testResumeCanarySoftwareUpgrade() {
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    UUID taskUUID = UUID.randomUUID();

    SoftwareUpgradeParams storedParams = new SoftwareUpgradeParams();
    storedParams.setUniverseUUID(u.getUniverseUUID());
    storedParams.ybSoftwareVersion = "2.21.0.0-b2";
    storedParams.clusters.add(u.getUniverseDetails().getPrimaryCluster());

    TaskInfo taskInfo = new TaskInfo(TaskType.SoftwareUpgradeYB, null);
    taskInfo.setUuid(taskUUID);
    taskInfo.setTaskState(TaskInfo.State.Paused);
    taskInfo.setTaskParams(Json.toJson(storedParams));
    taskInfo.setOwner("test");
    taskInfo.save();

    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              details.softwareUpgradeState =
                  UniverseDefinitionTaskParams.SoftwareUpgradeState.Paused;
              details.placementModificationTaskUuid = taskUUID;
              universe.setUniverseDetails(details);
            });

    persistSuccessfulCanaryChildSubtask(taskUUID);

    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class), any(UUID.class)))
        .thenAnswer(invocation -> invocation.getArgument(2));

    UUID result = handler.resumeCanarySoftwareUpgrade(c.getUuid(), u.getUniverseUUID(), taskUUID);

    assertEquals(taskUUID, result);
    ArgumentCaptor<TaskType> taskTypeCaptor = ArgumentCaptor.forClass(TaskType.class);
    ArgumentCaptor<SoftwareUpgradeParams> paramsCaptor =
        ArgumentCaptor.forClass(SoftwareUpgradeParams.class);
    ArgumentCaptor<UUID> uuidCaptor = ArgumentCaptor.forClass(UUID.class);
    verify(mockCommissioner)
        .submit(taskTypeCaptor.capture(), paramsCaptor.capture(), uuidCaptor.capture());
    assertEquals(TaskType.SoftwareUpgradeYB, taskTypeCaptor.getValue());
    assertEquals(u.getUniverseUUID(), paramsCaptor.getValue().getUniverseUUID());
    assertEquals(taskUUID, uuidCaptor.getValue());
  }

  @Test
  public void testResumeCanarySoftwareUpgradeRejectsNonPausedTask() {
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    UUID taskUUID = UUID.randomUUID();

    SoftwareUpgradeParams storedParams = new SoftwareUpgradeParams();
    storedParams.setUniverseUUID(u.getUniverseUUID());
    storedParams.clusters.add(u.getUniverseDetails().getPrimaryCluster());

    TaskInfo taskInfo = new TaskInfo(TaskType.SoftwareUpgradeYB, null);
    taskInfo.setUuid(taskUUID);
    taskInfo.setTaskState(TaskInfo.State.Success);
    taskInfo.setTaskParams(Json.toJson(storedParams));
    taskInfo.setOwner("test");
    taskInfo.save();

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.resumeCanarySoftwareUpgrade(c.getUuid(), u.getUniverseUUID(), taskUUID));
    assertEquals(400, e.getHttpStatus());
    verify(mockCommissioner, never()).submit(any(), any(), any(UUID.class));
  }

  @Test
  public void testResumeCanarySoftwareUpgradeRejectsWrongTaskType() {
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    UUID taskUUID = UUID.randomUUID();

    SoftwareUpgradeParams storedParams = new SoftwareUpgradeParams();
    storedParams.setUniverseUUID(u.getUniverseUUID());
    storedParams.clusters.add(u.getUniverseDetails().getPrimaryCluster());

    TaskInfo taskInfo = new TaskInfo(TaskType.GFlagsUpgrade, null);
    taskInfo.setUuid(taskUUID);
    taskInfo.setTaskState(TaskInfo.State.Paused);
    taskInfo.setTaskParams(Json.toJson(storedParams));
    taskInfo.setOwner("test");
    taskInfo.save();

    Universe updatedUniverse =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              details.softwareUpgradeState =
                  UniverseDefinitionTaskParams.SoftwareUpgradeState.Paused;
              details.updatingTaskUUID = taskUUID;
              universe.setUniverseDetails(details);
            });

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.resumeCanarySoftwareUpgrade(
                    c.getUuid(), updatedUniverse.getUniverseUUID(), taskUUID));
    assertEquals(400, e.getHttpStatus());
    assertTrue(e.getMessage().contains("not a software upgrade task"));
    verify(mockCommissioner, never()).submit(any(), any(), any(UUID.class));
  }

  @Test
  public void testResumeCanarySoftwareUpgradeRejectsUniverseNotPaused() {
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    UUID taskUUID = UUID.randomUUID();

    SoftwareUpgradeParams storedParams = new SoftwareUpgradeParams();
    storedParams.setUniverseUUID(u.getUniverseUUID());
    storedParams.clusters.add(u.getUniverseDetails().getPrimaryCluster());

    TaskInfo taskInfo = new TaskInfo(TaskType.SoftwareUpgradeYB, null);
    taskInfo.setUuid(taskUUID);
    taskInfo.setTaskState(TaskInfo.State.Paused);
    taskInfo.setTaskParams(Json.toJson(storedParams));
    taskInfo.setOwner("test");
    taskInfo.save();

    Universe updatedUniverse =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              details.softwareUpgradeState =
                  UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready;
              details.updatingTaskUUID = taskUUID;
              universe.setUniverseDetails(details);
            });

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.resumeCanarySoftwareUpgrade(
                    c.getUuid(), updatedUniverse.getUniverseUUID(), taskUUID));
    assertEquals(400, e.getHttpStatus());
    assertTrue(e.getMessage().contains("not in Paused"));
    verify(mockCommissioner, never()).submit(any(), any(), any(UUID.class));
  }

  @Test
  public void testResumeCanarySoftwareUpgradeRejectsTaskUUIDMismatch() {
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    UUID taskUUID = UUID.randomUUID();
    UUID otherTaskUUID = UUID.randomUUID();

    SoftwareUpgradeParams storedParams = new SoftwareUpgradeParams();
    storedParams.setUniverseUUID(u.getUniverseUUID());
    storedParams.clusters.add(u.getUniverseDetails().getPrimaryCluster());

    TaskInfo taskInfo = new TaskInfo(TaskType.SoftwareUpgradeYB, null);
    taskInfo.setUuid(taskUUID);
    taskInfo.setTaskState(TaskInfo.State.Paused);
    taskInfo.setTaskParams(Json.toJson(storedParams));
    taskInfo.setOwner("test");
    taskInfo.save();

    Universe updatedUniverse =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              details.softwareUpgradeState =
                  UniverseDefinitionTaskParams.SoftwareUpgradeState.Paused;
              details.placementModificationTaskUuid = otherTaskUUID;
              universe.setUniverseDetails(details);
            });

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.resumeCanarySoftwareUpgrade(
                    c.getUuid(), updatedUniverse.getUniverseUUID(), taskUUID));
    assertEquals(400, e.getHttpStatus());
    assertTrue(e.getMessage().contains("does not match"));
    verify(mockCommissioner, never()).submit(any(), any(), any(UUID.class));
  }

  @Test
  public void testResumeCanarySoftwareUpgradeRejectsWrongUniverse() {
    Customer c = ModelFactory.testCustomer();
    Universe u1 = ModelFactory.createUniverse("Universe1", c.getId());
    Universe u2 = ModelFactory.createUniverse("Universe2", c.getId());
    UUID taskUUID = UUID.randomUUID();

    SoftwareUpgradeParams storedParams = new SoftwareUpgradeParams();
    storedParams.setUniverseUUID(u1.getUniverseUUID());
    storedParams.clusters.add(u1.getUniverseDetails().getPrimaryCluster());

    TaskInfo taskInfo = new TaskInfo(TaskType.SoftwareUpgradeYB, null);
    taskInfo.setUuid(taskUUID);
    taskInfo.setTaskState(TaskInfo.State.Paused);
    taskInfo.setTaskParams(Json.toJson(storedParams));
    taskInfo.setOwner("test");
    taskInfo.save();

    Universe.saveDetails(
        u1.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams details = universe.getUniverseDetails();
          details.softwareUpgradeState = UniverseDefinitionTaskParams.SoftwareUpgradeState.Paused;
          details.updatingTaskUUID = taskUUID;
          universe.setUniverseDetails(details);
        });

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.resumeCanarySoftwareUpgrade(c.getUuid(), u2.getUniverseUUID(), taskUUID));
    assertEquals(400, e.getHttpStatus());
    assertTrue(e.getMessage().contains("does not belong"));
    verify(mockCommissioner, never()).submit(any(), any(), any(UUID.class));
  }

  @Test
  public void testResumeCanarySoftwareUpgradeUpdatesCustomerTask() {
    Customer c = ModelFactory.testCustomer();
    Universe u = ModelFactory.createUniverse(c.getId());
    UUID taskUUID = UUID.randomUUID();

    SoftwareUpgradeParams storedParams = new SoftwareUpgradeParams();
    storedParams.setUniverseUUID(u.getUniverseUUID());
    storedParams.clusters.add(u.getUniverseDetails().getPrimaryCluster());

    TaskInfo taskInfo = new TaskInfo(TaskType.SoftwareUpgradeYB, null);
    taskInfo.setUuid(taskUUID);
    taskInfo.setTaskState(TaskInfo.State.Paused);
    taskInfo.setTaskParams(Json.toJson(storedParams));
    taskInfo.setOwner("test");
    taskInfo.save();

    Universe updatedUniverse =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              details.softwareUpgradeState =
                  UniverseDefinitionTaskParams.SoftwareUpgradeState.Paused;
              details.placementModificationTaskUuid = taskUUID;
              universe.setUniverseDetails(details);
            });

    persistSuccessfulCanaryChildSubtask(taskUUID);

    CustomerTask customerTask =
        CustomerTask.create(
            c,
            updatedUniverse.getUniverseUUID(),
            taskUUID,
            CustomerTask.TargetType.Universe,
            CustomerTask.TaskType.SoftwareUpgrade,
            updatedUniverse.getName());
    customerTask.save();

    // Commissioner.submit(..., explicitTaskUuid) reuses the same task UUID (resume path).
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class), any(UUID.class)))
        .thenAnswer(invocation -> invocation.getArgument(2));

    UUID result =
        handler.resumeCanarySoftwareUpgrade(
            c.getUuid(), updatedUniverse.getUniverseUUID(), taskUUID);

    assertEquals(taskUUID, result);
    CustomerTask updatedTask = CustomerTask.findByTaskUUID(taskUUID);
    assertNotNull(updatedTask);
    assertEquals(updatedUniverse.getUniverseUUID(), updatedTask.getTargetUUID());
    assertNull(updatedTask.getCompletionTime());
  }

  private Universe createKubernetesUniverse(Customer customer) {
    return createKubernetesUniverseInternal(customer, null, true, true);
  }

  private Universe createKubernetesUniverseInternal(
      Customer customer,
      UUID rootCA,
      boolean enableNodeToNodeEncrypt,
      boolean enableClientToNodeEncrypt) {
    return createKubernetesUniverseInternal(
        customer, rootCA, enableNodeToNodeEncrypt, enableClientToNodeEncrypt, "2.28.0.0-b0");
  }

  private Universe createKubernetesUniverseInternal(
      Customer customer,
      UUID rootCA,
      boolean enableNodeToNodeEncrypt,
      boolean enableClientToNodeEncrypt,
      String ybSoftwareVersion) {
    Universe u = ModelFactory.createUniverse(customer.getId());
    u.updateConfig(ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    u.save();
    return Universe.saveDetails(
        u.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams details = universe.getUniverseDetails();
          details.getPrimaryCluster().userIntent.providerType = CloudType.kubernetes;
          details.getPrimaryCluster().userIntent.enableNodeToNodeEncrypt = enableNodeToNodeEncrypt;
          details.getPrimaryCluster().userIntent.enableClientToNodeEncrypt =
              enableClientToNodeEncrypt;
          details.getPrimaryCluster().userIntent.ybSoftwareVersion = ybSoftwareVersion;
          if (rootCA != null) {
            details.rootCA = rootCA;
          }
          universe.setUniverseDetails(details);
        });
  }

  // ==================== Cert-Manager Certificate Rotation Tests ====================

  @Test
  public void testRotateCertsKubernetesCertManagerRootCertRotation()
      throws IOException, NoSuchAlgorithmException {
    UUID fakeTaskUUID =
        FakeDBApplication.buildTaskInfo(null, TaskType.CertsRotateKubernetesUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Customer c = ModelFactory.testCustomer();
    // Use supported version for cert-manager cert rotation
    Universe u = createKubernetesUniverseInternal(c, null, true, false, "2026.1.0.0-b1");

    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.rootCA = UUID.randomUUID();
    params.rootAndClientRootCASame = true;

    // Create certificate info with K8SCertManager type
    String certBasePath = TestHelper.TMP_PATH + "/" + UUID.randomUUID();
    TestHelper.createTempFile(certBasePath, "test.crt", "test-cert");
    CertificateInfo.create(
        params.rootCA,
        c.getUuid(),
        "test-cert-manager-cert",
        new Date(),
        new Date(),
        "privateKey",
        certBasePath + "/test.crt",
        CertConfigType.K8SCertManager);

    handler.rotateCerts(params, c, u);

    ArgumentCaptor<CertsRotateParams> paramsArgumentCaptor =
        ArgumentCaptor.forClass(CertsRotateParams.class);
    verify(mockCommissioner)
        .submit(eq(TaskType.CertsRotateKubernetesUpgrade), paramsArgumentCaptor.capture());

    CertsRotateParams capturedParams = paramsArgumentCaptor.getValue();
    assertEquals(CertsRotateParams.CertRotationType.RootCert, capturedParams.rootCARotationType);
    assertEquals(params.rootCA, capturedParams.rootCA);
  }

  @Test
  public void testRotateCertsKubernetesCertManagerUnsupportedVersion()
      throws IOException, NoSuchAlgorithmException {
    Customer c = ModelFactory.testCustomer();
    // Use unsupported version for cert-manager cert rotation
    Universe u = createKubernetesUniverseInternal(c, null, true, false, "2025.2.0.0-b0");

    UUID certManagerRootCA = UUID.randomUUID();

    // Create certificate info with K8SCertManager type
    String certBasePath = TestHelper.TMP_PATH + "/" + UUID.randomUUID();
    TestHelper.createTempFile(certBasePath, "test.crt", "test-cert");
    CertificateInfo.create(
        certManagerRootCA,
        c.getUuid(),
        "test-cert-manager-cert",
        new Date(),
        new Date(),
        "privateKey",
        certBasePath + "/test.crt",
        CertConfigType.K8SCertManager);

    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.rootCA = certManagerRootCA;
    params.rootAndClientRootCASame = true;

    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> handler.rotateCerts(params, c, u));

    assertEquals(
        "Certificate rotation cert manager managed certificates is not supported for this"
            + " version. Please upgrade to a supported version.",
        exception.getMessage());
  }

  @Test
  public void testRotateCertsKubernetesCertManagerWithExistingCertManagerCert()
      throws IOException, NoSuchAlgorithmException {
    UUID fakeTaskUUID =
        FakeDBApplication.buildTaskInfo(null, TaskType.CertsRotateKubernetesUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Customer c = ModelFactory.testCustomer();
    UUID existingCertManagerCA = UUID.randomUUID();

    // Create certificate info for existing cert-manager cert
    String certBasePath = TestHelper.TMP_PATH + "/" + UUID.randomUUID();
    TestHelper.createTempFile(certBasePath, "existing.crt", "existing-cert");
    CertificateInfo.create(
        existingCertManagerCA,
        c.getUuid(),
        "existing-cert-manager-cert",
        new Date(),
        new Date(),
        "privateKey",
        certBasePath + "/existing.crt",
        CertConfigType.K8SCertManager);

    // Use supported version and set existing cert-manager rootCA
    Universe u =
        createKubernetesUniverseInternal(c, existingCertManagerCA, true, true, "2026.1.0.0-b1");

    UUID newCertManagerCA = UUID.randomUUID();

    // Create certificate info for new cert-manager cert
    TestHelper.createTempFile(certBasePath, "new.crt", "new-cert");
    CertificateInfo.create(
        newCertManagerCA,
        c.getUuid(),
        "new-cert-manager-cert",
        new Date(),
        new Date(),
        "privateKey",
        certBasePath + "/new.crt",
        CertConfigType.K8SCertManager);

    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.rootCA = newCertManagerCA;
    params.rootAndClientRootCASame = true;

    handler.rotateCerts(params, c, u);

    ArgumentCaptor<CertsRotateParams> paramsArgumentCaptor =
        ArgumentCaptor.forClass(CertsRotateParams.class);
    verify(mockCommissioner)
        .submit(eq(TaskType.CertsRotateKubernetesUpgrade), paramsArgumentCaptor.capture());

    CertsRotateParams capturedParams = paramsArgumentCaptor.getValue();
    assertEquals(CertsRotateParams.CertRotationType.RootCert, capturedParams.rootCARotationType);
    assertEquals(newCertManagerCA, capturedParams.rootCA);
  }

  @Test
  public void testRotateCertsKubernetesCertManagerServerCertRotation()
      throws IOException, NoSuchAlgorithmException {
    UUID fakeTaskUUID =
        FakeDBApplication.buildTaskInfo(null, TaskType.CertsRotateKubernetesUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Customer c = ModelFactory.testCustomer();
    UUID existingCertManagerCA = UUID.randomUUID();

    // Create certificate info for existing cert-manager cert
    String certBasePath = TestHelper.TMP_PATH + "/" + UUID.randomUUID();
    TestHelper.createTempFile(certBasePath, "existing.crt", "existing-cert");
    CertificateInfo.create(
        existingCertManagerCA,
        c.getUuid(),
        "existing-cert-manager-cert",
        new Date(),
        new Date(),
        "privateKey",
        certBasePath + "/existing.crt",
        CertConfigType.K8SCertManager);

    // Use supported version and set existing cert-manager rootCA
    Universe u =
        createKubernetesUniverseInternal(c, existingCertManagerCA, true, false, "2026.1.0.0-b1");

    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.rootCA = existingCertManagerCA; // Same rootCA
    params.selfSignedServerCertRotate = true;
    params.rootAndClientRootCASame = true;

    handler.rotateCerts(params, c, u);

    ArgumentCaptor<CertsRotateParams> paramsArgumentCaptor =
        ArgumentCaptor.forClass(CertsRotateParams.class);
    verify(mockCommissioner)
        .submit(eq(TaskType.CertsRotateKubernetesUpgrade), paramsArgumentCaptor.capture());

    CertsRotateParams capturedParams = paramsArgumentCaptor.getValue();
    assertEquals(CertsRotateParams.CertRotationType.ServerCert, capturedParams.rootCARotationType);
  }
}
