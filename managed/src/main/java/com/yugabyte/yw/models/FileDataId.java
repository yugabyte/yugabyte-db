/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import jakarta.persistence.Column;
import jakarta.persistence.Embeddable;
import java.io.Serializable;
import lombok.Data;

@Embeddable
@Data
public class FileDataId implements Serializable {

  @Column(name = "file_path")
  public String filePath;

  @Column(name = "extension")
  public String fileExtension;

  public FileDataId(String filePath, String fileExtension) {
    this.filePath = filePath;
    this.fileExtension = fileExtension;
  }
}
