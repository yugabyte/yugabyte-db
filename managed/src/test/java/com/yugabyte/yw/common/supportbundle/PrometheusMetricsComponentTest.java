// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common.supportbundle;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Date;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.apache.commons.lang3.reflect.FieldUtils;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class PrometheusMetricsComponentTest {

  @Mock private MetricQueryHelper metricQueryHelper;
  @Mock private SupportBundleUtil supportBundleUtil;
  @Mock private RuntimeConfGetter confGetter;

  @Rule public TemporaryFolder tmp = new TemporaryFolder();

  private PrometheusMetricsComponent component;

  @Before
  public void setUp() throws Exception {
    component = new PrometheusMetricsComponent(metricQueryHelper, supportBundleUtil);
    FieldUtils.writeField(component, "confGetter", confGetter, true);
    when(confGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpBatchDurationInMins))
        .thenReturn(15);
    when(confGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpStepInSecs)).thenReturn(60);
  }

  @Test
  public void testShortWindowIsExportedForEveryNode() throws Exception {
    // A window shorter than one batch, and a first node with no data, whose empty output
    // directory used to be removed from under the next node.
    Date start = Date.from(Instant.parse("2024-01-01T10:00:23Z"));
    Date end = Date.from(Instant.parse("2024-01-01T10:15:00Z"));
    List<Map<String, String>> queries = new ArrayList<>();
    when(metricQueryHelper.queryRange(any()))
        .thenAnswer(
            invocation -> {
              Map<String, String> params = invocation.getArgument(0);
              queries.add(params);
              String result =
                  params.get("query").contains("yb-n2")
                      ? "[{\"metric\":{\"__name__\":\"up\"},\"values\":[[1704103223,\"1\"]]}]"
                      : "[]";
              return Json.parse(
                  "{\"status\":\"success\",\"data\":{\"resultType\":\"matrix\",\"result\":"
                      + result
                      + "}}");
            });
    File dir = new File(tmp.getRoot(), "master_export");

    component.exportNodeLevelMetrics(
        universe("yb-n1", "yb-n2"), start, end, "master_export", dir.getPath(), false);

    assertEquals(2, queries.size());
    for (Map<String, String> query : queries) {
      assertEquals("2024-01-01T10:00:23Z", query.get("start"));
      assertEquals("2024-01-01T10:15:00Z", query.get("end"));
    }
    String[] files = dir.list();
    assertEquals(1, files.length);
    assertThat(files[0], containsString("yb-n2"));
  }

  private Universe universe(String... nodeNames) {
    UniverseDefinitionTaskParams details = new UniverseDefinitionTaskParams();
    Set<NodeDetails> nodes = new LinkedHashSet<>();
    for (String nodeName : nodeNames) {
      NodeDetails node = new NodeDetails();
      node.nodeName = nodeName;
      nodes.add(node);
    }
    details.nodeDetailsSet = nodes;
    Universe universe = new Universe();
    universe.setUniverseDetails(details);
    return universe;
  }
}
