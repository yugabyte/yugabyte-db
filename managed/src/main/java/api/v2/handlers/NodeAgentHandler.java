// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.NOT_IMPLEMENTED;

import api.v2.models.NodeAgentUpgradeSpec;
import api.v2.models.YBATask;
import api.v2.utils.ApiControllerUtils;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.audit.AuditService;
import java.util.UUID;

public class NodeAgentHandler extends ApiControllerUtils {

  private final Commissioner commissioner;

  @Inject
  public NodeAgentHandler(AuditService auditService, Commissioner commissioner) {
    super(auditService);
    this.commissioner = commissioner;
  }

  public YBATask upgradeNodeAgent(
      UUID cUUID, UUID uniUUID, NodeAgentUpgradeSpec nodeAgentUpgradeSpec) {
    // TODO: Implement this method later.
    throw new PlatformServiceException(NOT_IMPLEMENTED, "Not implemented");
  }
}
