// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundlev2.cgroup.SupportBundleCgroupScriptExecutor;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestLoader;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleCdcStateDumpSupport;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleLogParseSupport;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleServerConfRedactionSupport;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.BundleDetails.FilesComponentSpec;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.modules.CustomObjectMapperModule;
import java.io.File;
import java.util.Collections;
import java.util.Date;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class FilesComponentEstimateTest {

  @Mock private NodeUniverseManager mockNodeUniverseManager;
  @Mock private ImageBundleUtil mockImageBundleUtil;
  @Mock private SupportBundleCgroupScriptExecutor mockCgroupScriptExecutor;
  @Mock private SupportBundleLogParseSupport mockLogParseSupport;
  @Mock private SupportBundleCdcStateDumpSupport mockCdcStateDumpSupport;
  @Mock private SupportBundleServerConfRedactionSupport mockServerConfRedactionSupport;
  @Mock private SupportBundleScriptEstimateSupport mockScriptEstimateSupport;
  @Mock private Config mockConfig;
  @Mock private RuntimeConfGetter mockConfGetter;

  @Test
  public void placeholderFileComponentUses10Kb() throws Exception {
    FilesComponentSpec gflags = new FilesComponentSpec();
    gflags.setComponentName("GFlags");

    SupportBundleFormData bundleData = new SupportBundleFormData();
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setFilesComponentSpecs(Collections.singletonList(gflags));

    FilesComponent component = newComponent();
    NodeDetails node = new NodeDetails();
    node.nodeName = "n1";

    Map<String, Long> sizes =
        component.getFilesListWithSizes(
            null, bundleData, bundleDetails, null, new Date(), new Date(), node);

    assertEquals(SupportBundleV2EstimateRegistry.PLACEHOLDER_10KB, sizes.get("GFlags").longValue());
  }

  /** An estimate uses a helper already on the node, or the scripts' fallback; it installs none. */
  @Test
  public void scriptEstimatesInstallNoHelper() throws Exception {
    FilesComponentSpec universeLogs = new FilesComponentSpec();
    universeLogs.setComponentName("UniverseLogs");
    universeLogs.setScriptPath("bin/node_utils.sh");
    FilesComponentSpec coreFiles = new FilesComponentSpec();
    coreFiles.setComponentName("CoreFiles");
    coreFiles.setScriptPath("bin/node_utils.sh");

    SupportBundleFormData bundleData = new SupportBundleFormData();
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setFilesComponentSpecs(java.util.List.of(universeLogs, coreFiles));
    when(mockScriptEstimateSupport.runNodeScriptEstimate(
            any(), any(), any(), any(), any(), any(), any(), any()))
        .thenReturn(1L);

    NodeDetails node = new NodeDetails();
    node.nodeName = "n1";
    newComponent()
        .getFilesListWithSizes(null, bundleData, bundleDetails, null, new Date(), new Date(), node);

    verifyNoInteractions(
        mockLogParseSupport, mockCdcStateDumpSupport, mockServerConfRedactionSupport);
    verify(mockScriptEstimateSupport, times(2))
        .runNodeScriptEstimate(any(), any(), any(), any(), any(), any(), any(), any());
  }

  /** The staging is removed even though the cancel left the thread interrupted. */
  @Test
  public void aCancelledEstimateStopsTheNodeAndStillCleansUp() {
    FilesComponentSpec universeLogs = new FilesComponentSpec();
    universeLogs.setComponentName("UniverseLogs");
    universeLogs.setScriptPath("bin/node_utils.sh");
    FilesComponentSpec coreFiles = new FilesComponentSpec();
    coreFiles.setComponentName("CoreFiles");
    coreFiles.setScriptPath("bin/node_utils.sh");
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setFilesComponentSpecs(java.util.List.of(universeLogs, coreFiles));
    when(mockScriptEstimateSupport.runNodeScriptEstimate(
            any(), any(), any(), any(), any(), any(), any(), any()))
        .thenAnswer(
            invocation -> {
              Thread.currentThread().interrupt();
              throw new CancellationException("cancelled");
            });
    boolean[] interruptedDuringCleanup = {true};
    doAnswer(
            invocation -> {
              interruptedDuringCleanup[0] = Thread.currentThread().isInterrupted();
              return null;
            })
        .when(mockScriptEstimateSupport)
        .cleanupStaging(any(), any());
    NodeDetails node = new NodeDetails();
    node.nodeName = "n1";

    assertThrows(
        CancellationException.class,
        () ->
            newComponent()
                .getFilesListWithSizes(
                    null,
                    new SupportBundleFormData(),
                    bundleDetails,
                    null,
                    new Date(),
                    new Date(),
                    node));

    assertTrue(Thread.interrupted());
    assertFalse(interruptedDuringCleanup[0]);
    verify(mockScriptEstimateSupport, times(1))
        .runNodeScriptEstimate(any(), any(), any(), any(), any(), any(), any(), any());
  }

  @Test
  public void anInterruptedEstimateRunsNoScript() {
    FilesComponentSpec universeLogs = new FilesComponentSpec();
    universeLogs.setComponentName("UniverseLogs");
    universeLogs.setScriptPath("bin/node_utils.sh");
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setFilesComponentSpecs(Collections.singletonList(universeLogs));
    NodeDetails node = new NodeDetails();
    node.nodeName = "n1";

    Thread.currentThread().interrupt();
    assertThrows(
        CancellationException.class,
        () ->
            newComponent()
                .getFilesListWithSizes(
                    null,
                    new SupportBundleFormData(),
                    bundleDetails,
                    null,
                    new Date(),
                    new Date(),
                    node));

    assertTrue(Thread.interrupted());
    verify(mockScriptEstimateSupport, never())
        .runNodeScriptEstimate(any(), any(), any(), any(), any(), any(), any(), any());
    verify(mockScriptEstimateSupport, times(1)).cleanupStaging(any(), any());
  }

  @Test
  public void bashEstimateFileComponentDelegatesToScriptSupport() throws Exception {
    FilesComponentSpec universeLogs = new FilesComponentSpec();
    universeLogs.setComponentName("UniverseLogs");
    universeLogs.setScriptPath("bin/node_utils.sh");

    SupportBundleFormData bundleData = new SupportBundleFormData();
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setFilesComponentSpecs(Collections.singletonList(universeLogs));

    when(mockScriptEstimateSupport.runNodeScriptEstimate(
            any(), any(), any(), any(), any(), any(), any(), any()))
        .thenReturn(42L);

    FilesComponent component = newComponent();
    NodeDetails node = new NodeDetails();
    node.nodeName = "n1";

    Map<String, Long> sizes =
        component.getFilesListWithSizes(
            null, bundleData, bundleDetails, null, new Date(), new Date(), node);

    assertEquals(42L, sizes.get("UniverseLogs").longValue());
  }

  @Test
  public void eachNodesEstimateStagingIsRemovedUnderTheIdItUsed() throws Exception {
    FilesComponentSpec universeLogs = new FilesComponentSpec();
    universeLogs.setComponentName("UniverseLogs");
    universeLogs.setScriptPath("bin/node_utils.sh");
    FilesComponentSpec coreFiles = new FilesComponentSpec();
    coreFiles.setComponentName("CoreFiles");
    coreFiles.setScriptPath("bin/node_utils.sh");
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setFilesComponentSpecs(java.util.List.of(universeLogs, coreFiles));
    ArgumentCaptor<UUID> stagingIds = ArgumentCaptor.forClass(UUID.class);
    when(mockScriptEstimateSupport.runNodeScriptEstimate(
            any(), any(), any(), any(), any(), any(), any(), stagingIds.capture()))
        .thenReturn(1L);
    NodeDetails node = new NodeDetails();
    node.nodeName = "n1";

    newComponent()
        .getFilesListWithSizes(
            null, new SupportBundleFormData(), bundleDetails, null, new Date(), new Date(), node);

    // One id for the node's specs, removed once afterwards.
    assertEquals(stagingIds.getAllValues().get(0), stagingIds.getAllValues().get(1));
    verify(mockScriptEstimateSupport, times(1))
        .cleanupStaging(eq(stagingIds.getAllValues().get(0)), any());
  }

  private FilesComponent newComponent() {
    return new FilesComponent(
        mockNodeUniverseManager,
        new SupportBundleRunAsSupport(
            mockImageBundleUtil,
            new SupportBundleV2ManifestLoader(
                ConfigFactory.parseString(
                    String.format("yb.devops.home = \"%s\"", new File("devops").getAbsolutePath())),
                CustomObjectMapperModule.createDefaultMapper())),
        mockCgroupScriptExecutor,
        mockLogParseSupport,
        mockCdcStateDumpSupport,
        mockServerConfRedactionSupport,
        mockScriptEstimateSupport,
        mockConfig,
        mockConfGetter);
  }
}
