// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.models.Customer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.models.Universe;

import java.util.UUID;

public class RemoveUniverseEntry extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(RemoveUniverseEntry.class);
  @Override
  protected DestroyUniverse.Params taskParams() {
    return (DestroyUniverse.Params) taskParams;
  }
  @Override
  public void run() {
    Customer customer = Customer.get(taskParams().customerUUID);
    customer.removeUniverseUUID(taskParams().universeUUID);
    customer.save();
    Universe.delete(taskParams().universeUUID);
  }
}
