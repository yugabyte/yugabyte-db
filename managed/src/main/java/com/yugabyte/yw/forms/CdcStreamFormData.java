package com.yugabyte.yw.forms;

import play.data.validation.Constraints;
import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
public class CdcStreamFormData {
  @Constraints.Required() private String databaseName;
}
