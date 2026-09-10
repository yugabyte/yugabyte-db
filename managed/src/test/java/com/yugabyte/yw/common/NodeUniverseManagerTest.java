// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.NodeAgentPoller;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.Silent.class)
public class NodeUniverseManagerTest extends FakeDBApplication {

  @InjectMocks NodeUniverseManager nodeUniverseManager;

  @Mock ShellProcessHandler shellProcessHandler;
  @Mock RuntimeConfGetter mockConfGetter;
  @Mock LocalNodeUniverseManager localNodeUniverseManager;
  @Mock NodeActionRunner nodeActionRunner;
  @Mock ImageBundleUtil imageBundleUtil;
  @Mock NodeAgentPoller nodeAgentPoller;
  @Mock NodeAgentClient nodeAgentClient;
  @Mock FileHelperService fileHelperService;

  private Provider defaultProvider;
  private ArgumentCaptor<List<String>> commandCaptor;
  private ArgumentCaptor<ShellProcessContext> shellProcessContextCaptor;

  @Before
  public void setUp() {
    defaultProvider = ModelFactory.awsProvider(ModelFactory.testCustomer());
    AccessKey.create(defaultProvider.getUuid(), "akc", new AccessKey.KeyInfo());
    commandCaptor = ArgumentCaptor.forClass(List.class);
    shellProcessContextCaptor = ArgumentCaptor.forClass(ShellProcessContext.class);
    when(nodeAgentClient.maybeGetAndUpgrade(any())).thenReturn(Optional.empty());
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.ssh2Enabled))).thenReturn(false);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.devopsCommandTimeout)))
        .thenReturn(Duration.ofHours(1));
    when(mockConfGetter.getConfForScope(
            any(Provider.class), eq(ProviderConfKeys.remoteTmpDirectory)))
        .thenReturn("/tmp");
  }

  @Test
  public void testRunYsqlBatchCommandsDisablesCommandLogging() {
    Universe universe =
        ModelFactory.createFromConfig(defaultProvider, "slow-query-test", "r1-az1-1-1");
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    String sensitiveQuery = "SELECT query, calls FROM pg_stat_statements WHERE query LIKE '%ssn%'";

    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "ok"));

    nodeUniverseManager.runYsqlBatchCommands(
        node, universe, "postgres", List.of(sensitiveQuery), 180L, false, false, false);

    verify(shellProcessHandler).run(commandCaptor.capture(), shellProcessContextCaptor.capture());
    ShellProcessContext context = shellProcessContextCaptor.getValue();
    assertFalse(context.isLogCmdOutput());
    assertTrue(context.getRedactedVals().containsValue(Util.REDACTED_YSQL_QUERY));
    assertTrue(
        context.getRedactedVals().keySet().stream()
            .anyMatch(val -> val.contains("pg_stat_statements")));
  }

  @Test
  public void testRunYsqlBatchCommandsLogsCommandByDefault() {
    Universe universe =
        ModelFactory.createFromConfig(defaultProvider, "slow-query-test-2", "r1-az1-1-1");
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    String query = "SELECT 1";

    when(shellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, "ok"));

    nodeUniverseManager.runYsqlBatchCommands(
        node, universe, "postgres", List.of(query), 180L, false, false, true);

    verify(shellProcessHandler).run(commandCaptor.capture(), shellProcessContextCaptor.capture());
    ShellProcessContext context = shellProcessContextCaptor.getValue();
    assertTrue(context.isLogCmdOutput());
    assertTrue(context.getRedactedVals().isEmpty());
  }
}
