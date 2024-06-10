// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.gflags;

import java.util.ArrayList;
import java.util.List;

public class GFlagsAuditPayload {

  public List<GFlagDiffEntry> master = new ArrayList<GFlagDiffEntry>();
  public List<GFlagDiffEntry> tserver = new ArrayList<GFlagDiffEntry>();
  ;
}
