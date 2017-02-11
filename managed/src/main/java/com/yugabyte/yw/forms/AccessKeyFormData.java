// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.AccessManager;
import play.data.validation.Constraints;

public class AccessKeyFormData {
    @Constraints.Required()
    public String keyCode;

    public AccessManager.KeyType keyType;
}
