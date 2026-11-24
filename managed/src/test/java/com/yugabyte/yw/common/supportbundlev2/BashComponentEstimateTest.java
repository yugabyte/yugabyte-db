// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static org.junit.Assert.assertEquals;

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
import com.yugabyte.yw.models.helpers.BundleDetails.BashComponentSpec;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.modules.CustomObjectMapperModule;
import java.io.File;
import java.util.Collections;
import java.util.Date;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class BashComponentEstimateTest {

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
  public void getFilesListWithSizesReturnsPlaceholderPerSpec() throws Exception {
    BashComponentSpec spec = new BashComponentSpec();
    spec.setComponentName("collect_top_ps_pmap_stats");

    SupportBundleFormData bundleData = new SupportBundleFormData();
    BundleDetails bundleDetails = new BundleDetails();
    bundleDetails.setBashComponentSpecs(Collections.singletonList(spec));

    BashComponent component =
        new BashComponent(
            mockNodeUniverseManager,
            new SupportBundleRunAsSupport(
                mockImageBundleUtil,
                new SupportBundleV2ManifestLoader(
                    ConfigFactory.parseString(
                        String.format(
                            "yb.devops.home = \"%s\"", new File("devops").getAbsolutePath())),
                    CustomObjectMapperModule.createDefaultMapper())),
            mockCgroupScriptExecutor,
            mockLogParseSupport,
            mockCdcStateDumpSupport,
            mockServerConfRedactionSupport,
            mockScriptEstimateSupport,
            mockConfig,
            mockConfGetter);

    NodeDetails node = new NodeDetails();
    node.nodeName = "n1";

    Map<String, Long> sizes =
        component.getFilesListWithSizes(
            null, bundleData, bundleDetails, null, new Date(), new Date(), node);

    assertEquals(1, sizes.size());
    assertEquals(
        SupportBundleV2EstimateRegistry.PLACEHOLDER_5KB,
        sizes.get("collect_top_ps_pmap_stats").longValue());
  }
}
