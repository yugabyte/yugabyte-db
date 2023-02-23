// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.AlertTemplateVariable;
import java.util.List;
import javax.validation.Valid;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;

public class AlertTemplateVariablesFormData {

  @Valid
  @NotNull
  @Size(min = 1)
  public List<AlertTemplateVariable> variables;
}
