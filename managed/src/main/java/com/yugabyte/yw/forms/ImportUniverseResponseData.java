package com.yugabyte.yw.forms;

import com.yugabyte.yw.forms.ImportUniverseFormData.State;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

public class ImportUniverseResponseData {

  public State state;
  public String masterAddresses;
  public String universeName;
  public Map<String, String> checks = new HashMap<>();
  public String error;
  public UUID universeUUID;
  @Deprecated public int tservers_count;
  public List<String> tservers_list = new ArrayList<>();
}
