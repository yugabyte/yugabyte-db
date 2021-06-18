// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.forms.AlertingFormData.AlertingData;
import com.yugabyte.yw.common.alerts.SmtpData;
import java.util.List;

/** This class will be used by Customer details API. */
public class CustomerDetailsData {

  public String uuid;

  public String code;

  public String name;

  public String creationDate;

  public JsonNode features;

  public List<String> universeUUIDs;

  public String process_start_time;

  public int customerId;

  public AlertingData alertingData;

  public SmtpData smtpData;

  public String callhomeLevel;
}
