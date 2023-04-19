package com.yugabyte.yw.models.helpers;

import lombok.Data;

@Data
public class NodeID {
  private String name;
  private String uuid;

  public NodeID(String name, String uuid) {
    this.name = name;
    this.uuid = uuid;
  }
}
