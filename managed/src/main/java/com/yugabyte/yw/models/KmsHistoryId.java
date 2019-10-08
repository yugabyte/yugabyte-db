/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import com.avaje.ebean.annotation.EnumValue;
import java.io.Serializable;
import java.util.UUID;
import javax.persistence.Embeddable;

@Embeddable
public class KmsHistoryId implements Serializable {
    public enum TargetType {
        @EnumValue("UNIVERSE_KEY")
        UNIVERSE_KEY;
    }

    public UUID configUUID;

    public UUID targetUUID;

    public TargetType type;

    public KmsHistoryId(UUID configUUID, UUID targetUUID, TargetType type) {
        this.configUUID = configUUID;
        this.targetUUID = targetUUID;
        this.type = type;
    }

    @Override
    public boolean equals(Object o) {
        if (!(o instanceof KmsHistoryId)) return false;
        KmsHistoryId oCast = (KmsHistoryId) o;
        return oCast.configUUID.equals(this.configUUID) && oCast.targetUUID.equals(this.targetUUID);
    }

    @Override
    public int hashCode() {
        int result = 11;
        result = 31 * result + configUUID.hashCode();
        result = 31 * result + targetUUID.hashCode();
        result = 31 * result + type.hashCode();
        return result;
    }
}
