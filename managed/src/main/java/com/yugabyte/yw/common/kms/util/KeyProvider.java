/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.util;

import com.avaje.ebean.annotation.EnumValue;
import com.yugabyte.yw.common.kms.services.*;

/**
 * A list of third party encryption key providers that YB currently supports and the
 * corresponding service impl and any already instantiated classes
 * (such that each impl is a singleton)
 */
public enum KeyProvider {
    @EnumValue("AWS")
    AWS(AwsEARService.class),

    @EnumValue("SMARTKEY")
    SMARTKEY(SmartKeyEARService.class);

    private Class providerService;

    private EncryptionAtRestService instance;

    public Class getProviderService() {
        return this.providerService;
    }

    public EncryptionAtRestService getServiceInstance() { return this.instance; }

    public void setServiceInstance(EncryptionAtRestService instance) {
        this.instance = instance;
    }

    private KeyProvider(Class providerService) {
        this.providerService = providerService;
        this.instance = null;
    }
}
