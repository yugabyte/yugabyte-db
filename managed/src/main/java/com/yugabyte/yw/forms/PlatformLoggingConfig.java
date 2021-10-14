// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import javax.persistence.Enumerated;
import javax.persistence.EnumType;
import lombok.Getter;
import play.data.validation.Constraints;
import org.slf4j.event.Level;

public class PlatformLoggingConfig {

  @Constraints.Required()
  @Enumerated(EnumType.STRING)
  @Getter
  public Level level;
}
