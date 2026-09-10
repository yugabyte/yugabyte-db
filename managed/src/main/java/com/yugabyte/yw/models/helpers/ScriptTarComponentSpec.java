// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import java.util.List;

/** Common spec shape for node-level components that run a script and retrieve a remote tar. */
public interface ScriptTarComponentSpec {

  String getComponentName();

  String getScriptPath();

  List<String> getParams();

  String getRemoteTarPath();

  String getLinuxUser();

  long getTimeoutSecs();
}
