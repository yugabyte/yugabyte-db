// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static org.junit.Assert.assertEquals;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.BundleDetails.FilesComponentSpec;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.ScriptTarComponentSpec;
import java.util.Collection;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.slf4j.LoggerFactory;

/**
 * On kubernetes a {@link NodeDetails} is a single pod, so sub-components that need the tserver
 * process must be skipped on master pods. Estimation goes through the same filter as collection.
 */
public class ScriptTarNodeComponentPodRoleTest extends FakeDBApplication {

  private static final long TEST_SIZE = 42L;

  /** Exercises the shared spec loop without needing any of the node execution collaborators. */
  private static class TestScriptTarComponent extends AbstractScriptTarNodeComponent {

    private final List<? extends ScriptTarComponentSpec> specs;

    TestScriptTarComponent(List<? extends ScriptTarComponentSpec> specs) {
      super(
          "TestComponent",
          null,
          null,
          null,
          null,
          null,
          null,
          org.mockito.Mockito.mock(SupportBundleScriptEstimateSupport.class),
          null,
          null);
      this.specs = specs;
    }

    @Override
    protected List<? extends ScriptTarComponentSpec> getSpecsFromBundleDetails(
        BundleDetails bundleDetails) {
      return specs;
    }

    @Override
    protected List<? extends ScriptTarComponentSpec> getSpecs(
        SupportBundleTaskParams supportBundleTaskParams) {
      return specs;
    }

    @Override
    protected long estimateSubComponentSize(
        ScriptTarComponentSpec spec,
        String componentName,
        SupportBundleFormData bundleData,
        Universe universe,
        NodeDetails node,
        Date startDate,
        Date endDate,
        UUID stagingId) {
      return TEST_SIZE;
    }
  }

  private Customer customer;
  private TestScriptTarComponent component;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    component = new TestScriptTarComponent(List.of(spec("GFlags"), spec("YbcLogs")));
  }

  private static FilesComponentSpec spec(String componentName) {
    FilesComponentSpec spec = new FilesComponentSpec();
    spec.setComponentName(componentName);
    return spec;
  }

  private static NodeDetails pod(String nodeName, boolean isMaster) {
    NodeDetails node = new NodeDetails();
    node.nodeName = nodeName;
    node.isMaster = isMaster;
    node.isTserver = !isMaster;
    return node;
  }

  private Map<String, Long> estimate(Universe universe, NodeDetails node) throws Exception {
    return component.getFilesListWithSizes(
        customer,
        new SupportBundleFormData(),
        new BundleDetails(),
        universe,
        new Date(0L),
        new Date(1L),
        node);
  }

  @Test
  public void testMasterPodSkipsTserverOnlySpecs() throws Exception {
    Universe k8sUniverse =
        ModelFactory.createUniverse("k8s-universe", customer.getId(), CloudType.kubernetes);

    assertEquals(Map.of("GFlags", TEST_SIZE), estimate(k8sUniverse, pod("yb-master-0", true)));
  }

  @Test
  public void testTserverPodKeepsTserverOnlySpecs() throws Exception {
    Universe k8sUniverse =
        ModelFactory.createUniverse("k8s-universe", customer.getId(), CloudType.kubernetes);

    assertEquals(
        Map.of("GFlags", TEST_SIZE, "YbcLogs", TEST_SIZE),
        estimate(k8sUniverse, pod("yb-tserver-0", false)));
  }

  @Test
  public void testNonK8sNodeKeepsTserverOnlySpecsRegardlessOfRole() throws Exception {
    Universe awsUniverse = ModelFactory.createUniverse("aws-universe", customer.getId());

    assertEquals(
        Map.of("GFlags", TEST_SIZE, "YbcLogs", TEST_SIZE), estimate(awsUniverse, pod("n1", true)));
  }

  private static final List<String> LOCAL_TSERVER_MEMBERS =
      List.of("collect_cdc_state", "collect_threadz_memz_rpcz_stats");

  private static TestScriptTarComponent withLocalTserverMembers() {
    return new TestScriptTarComponent(
        List.of(
            spec("GFlags"),
            spec("YbcLogs"),
            spec("collect_cdc_state"),
            spec("collect_threadz_memz_rpcz_stats")));
  }

  /**
   * A dedicated master VM has no tserver for cdc_state's yb-ts-cli or threadz's curls; YbcLogs
   * still runs there, since yb-controller runs on master VMs too.
   */
  @Test
  public void testDedicatedMasterVmSkipsLocalTserverMembers() throws Exception {
    Universe awsUniverse = ModelFactory.createUniverse("aws-universe", customer.getId());
    component = withLocalTserverMembers();

    assertEquals(
        Map.of("GFlags", TEST_SIZE, "YbcLogs", TEST_SIZE), estimate(awsUniverse, pod("m1", true)));
    assertEquals(
        Map.of(
            "GFlags",
            TEST_SIZE,
            "YbcLogs",
            TEST_SIZE,
            "collect_cdc_state",
            TEST_SIZE,
            "collect_threadz_memz_rpcz_stats",
            TEST_SIZE),
        estimate(awsUniverse, pod("t1", false)));
  }

  @Test
  public void testMasterPodSkipsLocalTserverMembersAsBefore() throws Exception {
    Universe k8sUniverse =
        ModelFactory.createUniverse("k8s-universe", customer.getId(), CloudType.kubernetes);
    component = withLocalTserverMembers();

    assertEquals(Map.of("GFlags", TEST_SIZE), estimate(k8sUniverse, pod("yb-master-0", true)));
  }

  /** Collection skips them too, before running anything on the node. */
  @Test
  public void testDedicatedMasterVmCollectionSkipsLocalTserverMembers() throws Exception {
    Universe awsUniverse = ModelFactory.createUniverse("aws-universe", customer.getId());
    component =
        new TestScriptTarComponent(
            LOCAL_TSERVER_MEMBERS.stream().map(name -> spec(name)).collect(Collectors.toList()));

    assertOnlySkipped(collect(awsUniverse, pod("m1", true)), "m1", LOCAL_TSERVER_MEMBERS);
  }

  /** On Kubernetes, collection also skips YbcLogs on a master pod: YBC is the tserver's sidecar. */
  @Test
  public void testMasterPodCollectionSkipsYbcLogs() throws Exception {
    Universe k8sUniverse =
        ModelFactory.createUniverse("k8s-universe", customer.getId(), CloudType.kubernetes);
    component = new TestScriptTarComponent(List.of(spec("YbcLogs")));

    assertOnlySkipped(
        collect(k8sUniverse, pod("yb-master-0", true)), "yb-master-0", List.of("YbcLogs"));
  }

  private List<ILoggingEvent> collect(Universe universe, NodeDetails node) throws Exception {
    SupportBundleTaskParams taskParams = new SupportBundleTaskParams();
    taskParams.bundleData = new SupportBundleFormData();
    Logger logger = (Logger) LoggerFactory.getLogger(AbstractScriptTarNodeComponent.class);
    ListAppender<ILoggingEvent> appender = new ListAppender<>();
    appender.start();
    logger.addAppender(appender);
    try {
      component.downloadComponentBetweenDates(
          taskParams, customer, universe, null, new Date(0L), new Date(1L), node);
    } finally {
      logger.detachAppender(appender);
    }
    return appender.list;
  }

  private static void assertOnlySkipped(
      List<ILoggingEvent> events, String nodeName, Collection<String> names) {
    // No ERROR: the specs never reached collectSpec, whose collaborators are null here.
    assertEquals(
        List.of(),
        events.stream()
            .filter(event -> event.getLevel() == Level.ERROR)
            .map(ILoggingEvent::getFormattedMessage)
            .collect(Collectors.toList()));
    assertEquals(
        names.stream()
            .map(
                name ->
                    "Skipping TestComponent spec '"
                        + name
                        + "' on node '"
                        + nodeName
                        + "': it needs the tserver process, which this node does not run.")
            .collect(Collectors.toList()),
        events.stream()
            .filter(event -> event.getLevel() == Level.INFO)
            .map(ILoggingEvent::getFormattedMessage)
            .filter(message -> message.startsWith("Skipping"))
            .collect(Collectors.toList()));
  }
}
