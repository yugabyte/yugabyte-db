// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.AccessKey;
import play.data.validation.Constraints;

public class AccessKeyFormData {
    @Constraints.Required()
    public String keyCode;

    @Constraints.Required()
    public AccessKey.KeyInfo keyInfo;
}
