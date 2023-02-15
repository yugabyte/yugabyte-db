package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.AlertTemplateVariable;
import java.util.List;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
public class AlertTemplateVariablesList {
  private List<AlertTemplateSystemVariable> systemVariables;
  private List<AlertTemplateVariable> customVariables;
}
