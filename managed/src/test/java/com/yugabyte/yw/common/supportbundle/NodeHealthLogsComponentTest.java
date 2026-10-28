// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class NodeHealthLogsComponentTest extends FakeDBApplication {
  @Mock public UniverseInfoHandler mockUniverseInfoHandler;
  @Mock public NodeUniverseManager mockNodeUniverseManager;

  private static final String YB_HOME = "/home/yugabyte";
  private static final String METRICS_DIR = YB_HOME + "/metrics";
  private static final String LOGS_DIR = METRICS_DIR + "/logs";

  // One health log per day, plus a metrics textfile that carries no date at all.
  private static final String LOG_0910 = LOGS_DIR + "/node_health-20260910.log.gz";
  private static final String LOG_0912 = LOGS_DIR + "/node_health-20260912.log.gz";
  private static final String LOG_0914 = LOGS_DIR + "/node_health-20260914.log";
  private static final String METRIC_FILE = METRICS_DIR + "/node_health.prom";

  private NodeHealthLogsComponent component;
  private Universe universe;
  private Customer customer;
  private final NodeDetails node = new NodeDetails();

  @Before
  public void setUp() throws Exception {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    node.nodeName = "u-n1";
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "1.2.3.4";

    component =
        new NodeHealthLogsComponent(
            mockUniverseInfoHandler, mockNodeUniverseManager, new SupportBundleUtil());

    when(mockNodeUniverseManager.getYbHomeDir(any(), any())).thenReturn(YB_HOME);
    when(mockNodeUniverseManager.checkNodeIfFileExists(any(), any(), any())).thenReturn(true);

    Map<String, Long> metricsFiles = new HashMap<>();
    metricsFiles.put(METRIC_FILE, 10L);
    Map<String, Long> healthLogs = new HashMap<>();
    for (String path : Arrays.asList(LOG_0910, LOG_0912, LOG_0914)) {
      healthLogs.put(path, 10L);
    }
    // The component lists the two directories separately; the health logs path has a trailing
    // slash so find follows the symlink onto the data volume.
    when(mockNodeUniverseManager.getNodeFilePathAndSizes(
            any(), any(), eq(METRICS_DIR), eq(1), eq("f")))
        .thenReturn(metricsFiles);
    when(mockNodeUniverseManager.getNodeFilePathAndSizes(
            any(), any(), eq(LOGS_DIR + "/"), eq(1), eq("f")))
        .thenReturn(healthLogs);
  }

  private Date date(String yyyyMMdd) throws Exception {
    return new SimpleDateFormat("yyyyMMdd").parse(yyyyMMdd);
  }

  @Test
  public void testNoDateRangeTakesEverything() throws Exception {
    Set<String> paths =
        component.getFilesListWithSizes(customer, null, universe, null, null, node).keySet();

    assertEquals(4, paths.size());
    assertTrue(paths.containsAll(Arrays.asList(LOG_0910, LOG_0912, LOG_0914, METRIC_FILE)));
  }

  @Test
  public void testHealthLogsOutsideTheRangeAreLeftBehind() throws Exception {
    // A bundle asking for the 13th onwards must not carry the 10th.
    Set<String> paths =
        component
            .getFilesListWithSizes(
                customer, null, universe, date("20260913"), date("20260915"), node)
            .keySet();

    assertTrue("the in-range log is collected", paths.contains(LOG_0914));
    assertTrue("a log well before the range is not", !paths.contains(LOG_0910));
  }

  @Test
  public void testTheLogCurrentAtTheStartOfTheRangeIsKept() throws Exception {
    // The log being written when the window opened holds the entries from the start of it, so
    // filterFilePathsBetweenDates deliberately keeps the newest file before the start date.
    List<String> paths =
        Arrays.asList(
            component
                .getFilesListWithSizes(
                    customer, null, universe, date("20260913"), date("20260915"), node)
                .keySet()
                .toArray(new String[0]));

    assertTrue("the partially overlapping log is kept", paths.contains(LOG_0912));
  }

  @Test
  public void testMetricsFilesSurviveTheDateFilter() throws Exception {
    // The metrics textfiles carry no date in their names. Filtering by a log regex would drop
    // them silently, so they are deliberately not passed through it.
    Set<String> paths =
        component
            .getFilesListWithSizes(
                customer, null, universe, date("20260913"), date("20260915"), node)
            .keySet();

    assertTrue("metrics files are collected whatever the range", paths.contains(METRIC_FILE));
  }
}
