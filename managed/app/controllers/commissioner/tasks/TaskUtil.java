// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks;

import java.util.Collection;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.tasks.local.LocalAnsibleSetupServer;
import controllers.commissioner.tasks.local.LocalAnsibleConfigureServers;
import controllers.commissioner.tasks.local.LocalAnsibleClusterServerCtl;
import controllers.commissioner.tasks.local.LocalAnsibleUpdateNodeInfo;
import models.commissioner.InstanceInfo;
import models.commissioner.InstanceInfo.InstanceDetails;
import util.Util;

class TaskUtil {
  public static final Logger LOG = LoggerFactory.getLogger(TaskUtil.class);

  public static AnsibleSetupServer newAnsibleSetupServer() {
    if (!InstanceTaskBase.isLocalTesting) {
      return new AnsibleSetupServer();
    } else {
      return new LocalAnsibleSetupServer();
    }
  }

  public static AnsibleConfigureServers newAnsibleConfigureServers() {
    if (!InstanceTaskBase.isLocalTesting) {
      return new AnsibleConfigureServers();
    } else {
      return new LocalAnsibleConfigureServers();
    }
  }

  public static AnsibleClusterServerCtl newAnsibleClusterServerCtl() {
    if (!InstanceTaskBase.isLocalTesting) {
      return new AnsibleClusterServerCtl();
    } else {
      return new LocalAnsibleClusterServerCtl();
    }
  }

  public static AnsibleUpdateNodeInfo newAnsibleUpdateNodeInfo() {
    if (!InstanceTaskBase.isLocalTesting) {
      return new AnsibleUpdateNodeInfo();
    } else {
      return new LocalAnsibleUpdateNodeInfo();
    }
  }

  public static String getMasterHostPorts(UUID instanceUUID) {
    if (!InstanceTaskBase.isLocalTesting) {
      return InstanceInfo.getMasterAddresses(instanceUUID);
    } else {
      return Util.hostsAndPortsToString(InstanceTaskBase.miniCluster.getMasterHostPorts());
    }
  }
}
