package com.yugabyte.yw.forms;

import lombok.Getter;
import lombok.Setter;
import play.data.validation.Constraints;

@Getter
@Setter
public class CdcStreamFormData {
  @Constraints.Required() private String databaseName;
}
