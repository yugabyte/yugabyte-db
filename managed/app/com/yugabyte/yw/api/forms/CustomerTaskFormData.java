// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.forms;

import java.util.UUID;

public class CustomerTaskFormData {
  public UUID id;

  public String title;

  public int percentComplete;

	// We identify if the task was successful or not.
	// Based on that we provide different color coding on the UI
  public boolean success;
}
