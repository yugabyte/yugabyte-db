// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
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
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.GFlagDetails;
import com.yugabyte.yw.common.gflags.GFlagDiffEntry;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
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
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class UpgradeUniverseHandlerTest extends FakeDBApplication {
  private UpgradeUniverseHandler handler;
  private GFlagsAuditHandler gFlagsAuditHandler;
  private GFlagsValidationHandler gFlagsValidationHandler;

  @Before
  public void setUp() {
    Commissioner mockCommissioner = mock(Commissioner.class);
    when(mockCommissioner.submit(any(TaskType.class), any(ITaskParams.class)))
        .thenReturn(UUID.randomUUID());
    gFlagsValidationHandler = mock(GFlagsValidationHandler.class);
    gFlagsAuditHandler = new GFlagsAuditHandler(gFlagsValidationHandler);
    handler =
        new UpgradeUniverseHandler(
            mockCommissioner,
            mock(KubernetesManagerFactory.class),
            mock(RuntimeConfigFactory.class),
            mock(YbcManager.class),
            mock(RuntimeConfGetter.class),
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
    Assert.assertEquals(expected, result);
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
    Assert.assertEquals(1, gFlagDiffEntries.size());
    Assert.assertEquals("stderrthreshold", gFlagDiffEntries.get(0).name);
    Assert.assertEquals("1", gFlagDiffEntries.get(0).oldValue);
    Assert.assertEquals(null, gFlagDiffEntries.get(0).newValue);

    // added gflag
    oldGFlags.clear();
    newGFlags.clear();
    newGFlags.put("minloglevel", "2");
    gFlagDiffEntries =
        gFlagsAuditHandler.generateGFlagEntries(oldGFlags, newGFlags, serverType, softwareVersion);
    Assert.assertEquals("minloglevel", gFlagDiffEntries.get(0).name);
    Assert.assertEquals(null, gFlagDiffEntries.get(0).oldValue);
    Assert.assertEquals("2", gFlagDiffEntries.get(0).newValue);

    // updated gflag
    oldGFlags.clear();
    newGFlags.clear();
    oldGFlags.put("max_log_size", "0");
    newGFlags.put("max_log_size", "1000");
    gFlagDiffEntries =
        gFlagsAuditHandler.generateGFlagEntries(oldGFlags, newGFlags, serverType, softwareVersion);
    Assert.assertEquals("max_log_size", gFlagDiffEntries.get(0).name);
    Assert.assertEquals("0", gFlagDiffEntries.get(0).oldValue);
    Assert.assertEquals("1000", gFlagDiffEntries.get(0).newValue);

    // unchanged gflag
    oldGFlags.clear();
    newGFlags.clear();
    oldGFlags.put("max_log_size", "2000");
    newGFlags.put("max_log_size", "2000");
    gFlagDiffEntries =
        gFlagsAuditHandler.generateGFlagEntries(oldGFlags, newGFlags, serverType, softwareVersion);
    Assert.assertEquals(0, gFlagDiffEntries.size());
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
    Assert.assertEquals(expected, payload);
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
    Assert.assertEquals(expected, payload);
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
    Assert.assertEquals(expected, payload);
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
    Assert.assertEquals(expected, payload);
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
}
