package com.yugabyte.yw.forms;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;

import play.libs.Json;

public class ImportUniverseResponseData {

  public String state;
  public String masterAddresses;
  public String universeName;
  public ObjectNode checks = Json.newObject();
  public String error;
  public String universeUUID;
  public int tservers_count;
  public ArrayNode tservers_list = Json.newArray();
}
