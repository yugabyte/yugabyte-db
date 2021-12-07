// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;

@FunctionalInterface
public interface IUpgradeUniverseHandlerMethod<T extends UpgradeTaskParams> {
  UUID upgrade(T requestParams, Customer customer, Universe universe);
}
