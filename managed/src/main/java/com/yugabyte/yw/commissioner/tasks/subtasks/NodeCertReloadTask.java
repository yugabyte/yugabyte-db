package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

/**
 * This class interacts with the YBClient and get the certs reload done for every node
 *
 * @author msoundar
 */
@Slf4j
public class NodeCertReloadTask extends NodeTaskBase {

  // AbstractTaskBase has this, but just to make it TDD ready
  protected Params params;
  protected YBClientService clientService;

  @Inject
  protected NodeCertReloadTask(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
    this.clientService = this.ybService;
  }

  @Override
  public void run() {
    Params params = this.params;
    if (params == null) {
      throw new IllegalStateException("Task params are not initialized");
    }

    // fetch cert file from DB, it might have been changed during rolling restart
    String certFile = Universe.getOrBadRequest(params.getUniverseUUID()).getCertificateNodetoNode();

    YBClient client = null;
    try {
      client = clientService.getClient(params.masterHostPorts, certFile);
      log.info("about to reload certs for {} using certFile {}", params.nodeHostPort, certFile);

      client.reloadCertificates(HostAndPort.fromString(params.nodeHostPort));

    } catch (Exception e) {
      log.error("Certificate reload failed for node -> {}", params.nodeHostPort, e);
      throw new RuntimeException(e);

    } finally {
      clientService.closeClient(client, params.masterHostPorts);
    }

    log.info("Certificates reloaded for node -> {}", params.nodeHostPort);
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  public void initialize(Params params) {
    this.setParams(params);
    super.initialize(params);
  }

  public void setParams(Params params) {
    this.params = params;
  }

  public Params getParams() {
    return params;
  }

  protected YBClientService getClientService() {
    return this.clientService;
  }

  protected void setClientService(YBClientService service) {
    this.clientService = service;
  }

  public static class Params extends NodeTaskParams {
    String masterHostPorts;
    String nodeHostPort;

    public String getMasters() {
      return masterHostPorts;
    }

    public void setMasters(String masters) {
      this.masterHostPorts = masters;
    }

    public String getNode() {
      return nodeHostPort;
    }

    public void setNode(String node) {
      this.nodeHostPort = node;
    }
  }

  public static NodeCertReloadTask createTask() {
    return NodeTaskBase.createTask(NodeCertReloadTask.class);
  }
}
