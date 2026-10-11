// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import api.v2.mappers.NodeAgentMapper;
import api.v2.models.NodeAgentUpgradeSpec;
import api.v2.models.YBATask;
import api.v2.utils.ApiControllerUtils;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.UpgradeNodeAgent;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;

public class NodeAgentHandler extends ApiControllerUtils {

  private final Commissioner commissioner;

  @Inject
  public NodeAgentHandler(AuditService auditService, Commissioner commissioner) {
    super(auditService);
    this.commissioner = commissioner;
  }

  public YBATask upgradeNodeAgent(
      UUID cUUID, UUID uniUUID, NodeAgentUpgradeSpec nodeAgentUpgradeSpec) {
    Customer customer = Customer.getOrBadRequest(cUUID);
    Universe universe = Universe.getOrBadRequest(uniUUID, customer);
    UpgradeNodeAgent.Params taskParams =
        NodeAgentMapper.INSTANCE.toUpgradeNodeAgentParams(nodeAgentUpgradeSpec);
    taskParams.setUniverseUUID(uniUUID);
    if (StringUtils.isNotBlank(nodeAgentUpgradeSpec.getCertificateName())) {
      // Verify that the certificate exists and it belongs to the customer.
      taskParams.certificateUuid =
          CertificateInfo.getOrBadRequest(cUUID, nodeAgentUpgradeSpec.getCertificateName())
              .getUuid();
    }
    UUID taskUuid = commissioner.submit(TaskType.UpgradeNodeAgent, taskParams);
    CustomerTask.create(
        customer,
        uniUUID,
        taskUuid,
        CustomerTask.TargetType.NodeAgent,
        CustomerTask.TaskType.Update,
        universe.getName());
    return new YBATask().taskUuid(taskUuid).resourceUuid(uniUUID);
  }
}
