package com.yugabyte.yw.commissioner.tasks.params;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.forms.UniverseTaskParams;

public class ServerSubTaskParams extends UniverseTaskParams {
  // The name of the node which contains the server process.
  public String nodeName;

  // Server type running on the above node for which we will wait.
  public ServerType serverType;
}
