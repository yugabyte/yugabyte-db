// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertFalse;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.mockingDetails;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponent;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.EnumSet;
import java.util.HashSet;
import java.util.List;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.mockito.invocation.Invocation;

/**
 * Pins the layering: a component download subtask collects, and nothing more. Staged remote scripts
 * and child cgroups are torn down once per bundle by {@code CreateSupportBundleV2}, because subtask
 * groups run sequentially and a per-subtask hook would fire once per component.
 */
public class SupportBundleComponentDownloadTest extends FakeDBApplication {

  private Customer customer;
  private Universe universe;
  private final NodeDetails node = new NodeDetails();

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    node.nodeName = "u-n1";
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "10.0.0.1";
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.nodeDetailsSet = new HashSet<>(Arrays.asList(node));
              u.setUniverseDetails(details);
            });
    // Limits on, so the assertions below fail if a teardown reappears here rather than passing
    // because limits were off.
    mutableConfigFactory.globalRuntimeConf().setValue("yb.support_bundle.cgroup.enabled", "true");
  }

  @Test
  public void testComponentDownloadDoesNotTearDownRemoteScripts() {
    runDownload();

    // Every runCommand overload, so a teardown can't slip through one that isn't verified.
    List<List<String>> commands =
        mockingDetails(mockNodeUniverseManager).getInvocations().stream()
            .filter(inv -> inv.getMethod().getName().equals("runCommand"))
            .map(SupportBundleComponentDownloadTest::commandOf)
            .collect(Collectors.toList());
    assertFalse(commands.stream().anyMatch(cmd -> cmd.contains("rm") && cmd.contains("-rf")));
    assertFalse(commands.stream().anyMatch(cmd -> String.join(" ", cmd).contains("systemctl")));
  }

  @SuppressWarnings("unchecked")
  private static List<String> commandOf(Invocation invocation) {
    for (Object arg : invocation.getArguments()) {
      if (arg instanceof List) {
        return (List<String>) arg;
      }
      if (arg instanceof String) {
        return Collections.singletonList((String) arg);
      }
    }
    return Collections.emptyList();
  }

  private void runDownload() {
    SupportBundleFormData bundleData = new SupportBundleFormData();
    bundleData.components = EnumSet.of(ComponentType.UniverseLogs);
    SupportBundle bundle =
        SupportBundle.create(
            bundleData, universe, app.injector().instanceOf(RuntimeConfGetter.class));

    SupportBundleTaskParams v1TaskParams =
        new SupportBundleTaskParams(bundle, bundleData, customer, universe);

    SupportBundleComponentDownload.Params params =
        new SupportBundleComponentDownload.Params(
            mock(SupportBundleComponent.class),
            v1TaskParams,
            customer,
            universe,
            Paths.get("/tmp/does-not-need-to-exist"),
            node,
            new Date(0L),
            new Date());

    SupportBundleComponentDownload task =
        AbstractTaskBase.createTask(SupportBundleComponentDownload.class);
    task.initialize(params);
    task.run();
  }
}
