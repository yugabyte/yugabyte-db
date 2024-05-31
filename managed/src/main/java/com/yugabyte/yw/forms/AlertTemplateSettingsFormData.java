// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.AlertTemplateSettings;
import java.util.List;
import javax.validation.Valid;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;

public class AlertTemplateSettingsFormData {

  @Valid
  @NotNull
  @Size(min = 1)
  public List<AlertTemplateSettings> settings;
}
