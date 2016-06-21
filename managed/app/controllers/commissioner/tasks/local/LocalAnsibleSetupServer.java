// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks.local;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.tasks.AnsibleSetupServer;
import util.Util;

public class LocalAnsibleSetupServer extends AnsibleSetupServer {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleSetupServer.class);

  @Override
  public void run() {
    LOG.info("Local Testing mode, skipping devops setup.");
  }
}
