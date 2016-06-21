// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks.local;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.tasks.AnsibleConfigureServers;

public class LocalAnsibleConfigureServers extends AnsibleConfigureServers {
  public static final Logger LOG = LoggerFactory.getLogger(LocalAnsibleConfigureServers.class);
  @Override
  public void run() {
    LOG.info("Local Testing mode, skipping configure servers.");
  }
}
