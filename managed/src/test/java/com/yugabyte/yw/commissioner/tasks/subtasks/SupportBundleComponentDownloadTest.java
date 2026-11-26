// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellProcessContext;
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
import java.util.Date;
import java.util.EnumSet;
import java.util.HashSet;
import org.junit.Before;
import org.junit.Test;

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

    verify(mockNodeUniverseManager, never())
        .runCommand(
            eq(node),
            eq(universe),
            argThat(cmd -> cmd != null && cmd.contains("rm") && cmd.contains("-rf")),
            any(ShellProcessContext.class),
            eq(false));
    verify(mockNodeUniverseManager, never())
        .runCommand(
            eq(node),
            eq(universe),
            argThat(cmd -> cmd != null && String.join(" ", cmd).contains("systemctl")),
            any(ShellProcessContext.class),
            eq(false));
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
