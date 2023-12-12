// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.GFlagDetails;
import com.yugabyte.yw.common.gflags.GFlagDiffEntry;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeWithGFlags;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.Arrays;
import java.util.Collections;
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
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class UpgradeUniverseHandlerTest extends FakeDBApplication {
  private UpgradeUniverseHandler handler;
  private GFlagsAuditHandler gFlagsAuditHandler;
  private GFlagsValidationHandler gFlagsValidationHandler;
  private Commissioner mockCommissioner;
  private RuntimeConfGetter runtimeConfGetter;

  @Before
  public void setUp() {
    mockCommissioner = mock(Commissioner.class);
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
        .thenReturn(UUID.randomUUID());
    gFlagsValidationHandler = mock(GFlagsValidationHandler.class);
    gFlagsAuditHandler = new GFlagsAuditHandler(gFlagsValidationHandler);
    runtimeConfGetter = mock(RuntimeConfGetter.class);
    handler =
        new UpgradeUniverseHandler(
            mockCommissioner,
            mock(KubernetesManagerFactory.class),
            mock(RuntimeConfigFactory.class),
            mock(YbcManager.class),
            runtimeConfGetter,
            mock(CertificateHelper.class),
            mock(AutoFlagUtil.class),
            mock(XClusterUniverseService.class));
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
        SpecificGFlags.construct(Map.of("master", "1"), Map.of("tserver", "2"));
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
        SpecificGFlags.construct(Map.of("master", "5"), Map.of("tserver2", "2"));
    u =
        Universe.saveDetails(
            u.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithReadReplica(rrUserIntent, null));
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.getReadOnlyClusters().get(0).userIntent.specificGFlags =
        SpecificGFlags.construct(Map.of("master", "1"), Map.of("tserver", "2"));
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
                  SpecificGFlags.construct(Map.of("master", "1"), Map.of("tserver", "1"));
              universe.setUniverseDetails(details);
            });
    UniverseDefinitionTaskParams.UserIntent rrUserIntent =
        u.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    rrUserIntent.specificGFlags =
        SpecificGFlags.construct(Map.of("master", "5"), Map.of("tserver2", "2"));
    u =
        Universe.saveDetails(
            u.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithReadReplica(rrUserIntent, null));
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    params.getPrimaryCluster().userIntent.specificGFlags =
        SpecificGFlags.construct(Map.of("master", "2"), Map.of("tserver", "3"));
    params.getReadOnlyClusters().get(0).userIntent.specificGFlags =
        SpecificGFlags.construct(Map.of("master", "1"), Map.of("tserver", "2"));
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
        SpecificGFlags.construct(Map.of("master", "1"), Map.of("tserver", "2"));
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
                  SpecificGFlags.construct(Map.of("master", "1"), Map.of("tserver", "1"));
              userIntent.masterGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER);
              userIntent.tserverGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER);
              universe.setUniverseDetails(details);
            });
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    Map<String, String> masterGFlags = Map.of("asd", "10");
    Map<String, String> tserverGFlags = Map.of("awesd", "15");
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
                  SpecificGFlags.construct(Map.of("master", "1"), Map.of("tserver", "1"));
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
    Map<String, String> masterGFlags = Map.of("asd", "10");
    Map<String, String> tserverGFlags = Map.of("awesd", "15");
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
                  SpecificGFlags.construct(Map.of("master", "1"), Map.of("tserver", "1"));
              userIntent.masterGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER);
              userIntent.tserverGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER);
              universe.setUniverseDetails(details);
            });
    UniverseDefinitionTaskParams.UserIntent rrUserIntent =
        u.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    rrUserIntent.specificGFlags = SpecificGFlags.construct(Map.of("wer", "3"), Map.of("ere", "5"));
    u =
        Universe.saveDetails(
            u.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithReadReplica(rrUserIntent, null));
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    Map<String, String> masterGFlags = Map.of("asd", "10");
    Map<String, String> tserverGFlags = Map.of("awesd", "15");
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
                  SpecificGFlags.construct(Map.of("master", "1"), Map.of("tserver", "1"));
              userIntent.masterGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER);
              userIntent.tserverGFlags =
                  userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER);
              universe.setUniverseDetails(details);
            });
    UniverseDefinitionTaskParams.UserIntent rrUserIntent =
        u.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    rrUserIntent.specificGFlags = SpecificGFlags.construct(Map.of("wer", "3"), Map.of("ere", "5"));
    rrUserIntent.specificGFlags.setPerAZ(
        Map.of(
            UUID.randomUUID(),
            SpecificGFlags.construct(Map.of("a", "b"), Map.of("a", "b")).getPerProcessFlags()));
    u =
        Universe.saveDetails(
            u.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithReadReplica(rrUserIntent, null));
    GFlagsUpgradeParams params = new GFlagsUpgradeParams();
    params.setUniverseUUID(u.getUniverseUUID());
    params.clusters = u.getUniverseDetails().clusters;
    Map<String, String> masterGFlags = Map.of("asd", "10");
    Map<String, String> tserverGFlags = Map.of("awesd", "15");
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
                  SpecificGFlags.construct(Map.of("master", "1"), Map.of("tserver", "1"));
              userIntent.specificGFlags.setPerAZ(
                  Map.of(
                      UUID.randomUUID(),
                      SpecificGFlags.construct(Map.of("a", "b"), Map.of("a", "b"))
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
    params.masterGFlags = Map.of("asd", "10");
    params.tserverGFlags = Map.of("awesd", "15");
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
                  SpecificGFlags.construct(Map.of("master", "1"), Map.of("tserver", "1"));
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
                  SpecificGFlags.construct(Map.of("master", "1"), Map.of("tserver", "1"));
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

  //  private   public static UUID buildTaskInfo(UUID parentUUID, TaskType taskType) {
  //    TaskInfo taskInfo = new TaskInfo(taskType);
  //    taskInfo.setDetails(Json.newObject());
  //    taskInfo.setOwner("test-owner");
  //    if (parentUUID != null) {
  //      taskInfo.setParentUuid(parentUUID);
  //    }
  //    taskInfo.save();
  //    return taskInfo.getTaskUUID();
  //  }
}
