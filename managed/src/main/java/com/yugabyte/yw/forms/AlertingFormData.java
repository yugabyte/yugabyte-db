// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;


import play.data.validation.Constraints;


/**
 * This class will be used by the API and UI Form Elements to validate constraints are met
 */
public class AlertingFormData {
    @Constraints.MaxLength(15)
    public String code;

    public String email;

    public String password;

    public String confirmPassword;

    public String name;

    static public class AlertingData {
        @Constraints.Email
        @Constraints.MinLength(5)
        public String alertingEmail;

        public boolean sendAlertsToYb = false;

        public long checkIntervalMs = 0;

        public long statusUpdateIntervalMs = 0;

        public Boolean reportOnlyErrors = false;
    }

    public AlertingData alertingData;
}
