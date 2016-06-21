// Copyright (c) YugaByte, Inc.

package controllers.commissioner.tasks.local;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import controllers.commissioner.tasks.AnsibleClusterServerCtl;

public class LocalAnsibleClusterServerCtl extends AnsibleClusterServerCtl {
  public static final Logger LOG = LoggerFactory.getLogger(LocalAnsibleClusterServerCtl.class);

  @Override
  public void run() {
    LOG.info("Local Testing mode, skipping cluster server control.");
  }
}
