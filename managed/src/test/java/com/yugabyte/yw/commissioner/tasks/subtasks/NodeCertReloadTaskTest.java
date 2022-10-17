package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.Mockito.doCallRealMethod;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.mockStatic;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import java.util.UUID;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.MockedStatic;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.YBClient;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.subtasks.NodeCertReloadTask.Params;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;

@RunWith(MockitoJUnitRunner.class)
public class NodeCertReloadTaskTest {

  @Test
  public void testReload() {

    // we don't have play framework in test, so mock this class also
    // still call the real methods
    BaseTaskDependencies bd = mock(BaseTaskDependencies.class);
    NodeManager nm = mock(NodeManager.class);
    NodeCertReloadTask task = new NodeCertReloadTask(bd, nm);

    MockedStatic<NodeCertReloadTask> mockedTaskBaseClass = mockStatic(NodeCertReloadTask.class);
    mockedTaskBaseClass.when(() -> NodeCertReloadTask.createTask()).thenReturn(task);

    Universe mockedUniverse = mock(Universe.class);
    MockedStatic<Universe> mockedUniverseClass = mockStatic(Universe.class);
    mockedUniverseClass
        .when(() -> Universe.getOrBadRequest(any(UUID.class)))
        .thenReturn(mockedUniverse);
    when(mockedUniverse.getCertificateNodetoNode()).thenReturn("someCertFilePath");

    YBClient client = mock(YBClient.class);
    YBClientService service = mock(YBClientService.class);
    task.setClientService(service);
    when(service.getClient(any(), any())).thenReturn(client);
    when(client.reloadCertificates(any())).thenReturn(true);

    // cases
    Params params = new Params();
    params.universeUUID = UUID.randomUUID();
    params.setMasters("node1:7100");
    task.setParams(params);

    params.setNode("node1:7100");
    task.run();

    params.setNode("node1:9100");
    task.run();

    params.setNode("node2:9100");
    task.run();

    HostAndPort[] serverNames =
        new HostAndPort[] {
          HostAndPort.fromString("node1:7100"),
          HostAndPort.fromString("node1:9100"),
          HostAndPort.fromString("node2:9100")
        };
    verify(client, times(1))
        .reloadCertificates(
            argThat((HostAndPort server) -> server != null && server.equals(serverNames[0])));
    verify(client, times(1))
        .reloadCertificates(
            argThat((HostAndPort server) -> server != null && server.equals(serverNames[1])));
    verify(client, times(1))
        .reloadCertificates(
            argThat((HostAndPort server) -> server != null && server.equals(serverNames[2])));
  }
}
