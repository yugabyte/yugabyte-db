// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class SaveNode implements Runnable {
  public static final Logger LOG = LoggerFactory.getLogger(Universe.class);
  private final int iter;
  private final UUID uuid;

  public SaveNode(UUID uid, int it) {
    iter = it;
    uuid = uid;
  }

  @Override
  public void run() {
    Universe.UniverseUpdater updater =
        new Universe.UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            NodeDetails node = new NodeDetails();
            node.nodeName = "host-n" + iter;
            universeDetails.nodeDetailsSet.add(node);
            universe.setUniverseDetails(universeDetails);
          }
        };
    LOG.info("Running iter " + iter);
    Universe.saveDetails(uuid, updater);
  }
}
