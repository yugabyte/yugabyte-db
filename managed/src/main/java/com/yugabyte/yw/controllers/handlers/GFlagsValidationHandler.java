// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.common.GFlagDetails;
import com.yugabyte.yw.common.GFlagsValidation;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.GFlagsValidationParams.GFlagsValidationRequest;
import com.yugabyte.yw.forms.GFlagsValidationParams.GFlagsValidationResponse;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class GFlagsValidationHandler {

  @Inject private GFlagsValidation gflagsValidation;

  public static final Logger LOG = LoggerFactory.getLogger(GFlagsValidationHandler.class);

  public List<GFlagDetails> listGFlags(
      String version, String gflag, String serverType, Boolean mostUsedGFlags) throws IOException {
    validateServerType(serverType);
    validateVersionFormat(version);
    List<GFlagDetails> gflagsList =
        gflagsValidation.extractGFlags(version, serverType, mostUsedGFlags);
    if (StringUtils.isEmpty(gflag)) {
      return gflagsList;
    }
    List<GFlagDetails> result = new ArrayList<>();
    for (GFlagDetails flag : gflagsList) {
      if (flag.name.contains(gflag)) {
        result.add(flag);
      }
    }
    return result;
  }

  public GFlagsValidationResponse validateGFlags(String version, GFlagsValidationRequest gflags)
      throws IOException {
    validateVersionFormat(version);
    if (gflags.masterGFlags == null || gflags.tserverGFlags == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Either master and tserver gflags cannot be null");
    }

    GFlagsValidationResponse validationResponse = new GFlagsValidationResponse();
    // Check if there is any errors in tserver gflags
    List<GFlagDetails> masterGFlagsList =
        gflagsValidation.extractGFlags(version, ServerType.MASTER.toString(), false);
    validationResponse.masterGFlags = checkGFlagsError(gflags.masterGFlags, masterGFlagsList);

    // Check if there is any errors in tserver gflags
    List<GFlagDetails> tserverGFlagsList =
        gflagsValidation.extractGFlags(version, ServerType.TSERVER.toString(), false);
    validationResponse.tserverGFlags = checkGFlagsError(gflags.tserverGFlags, tserverGFlagsList);

    return validationResponse;
  }

  public GFlagDetails getGFlagsMetadata(String version, String serverType, String gflag)
      throws IOException {
    validateServerType(serverType);
    validateVersionFormat(version);
    List<GFlagDetails> gflagsList = gflagsValidation.extractGFlags(version, serverType, false);
    for (GFlagDetails flag : gflagsList) {
      if (flag.name.equals(gflag)) {
        return flag;
      }
    }
    throw new PlatformServiceException(BAD_REQUEST, gflag + " is not present in metadata.");
  }

  private Map<String, Map<String, String>> checkGFlagsError(
      Map<String, String> gflags, List<GFlagDetails> gflagsList) throws IOException {
    Map<String, Map<String, String>> errorObject = new HashMap<>();
    for (Map.Entry<String, String> userGFlag : gflags.entrySet()) {
      boolean found = false;
      Map<String, String> errorSource = new HashMap<>();
      for (GFlagDetails flag : gflagsList) {
        if (flag.name.equals(userGFlag.getKey())) {
          String errorMsg = checkValueType(userGFlag.getValue(), flag.type);
          if (!errorMsg.isEmpty()) {
            errorSource.put("type", errorMsg);
          }
          found = true;
          break;
        }
      }
      if (!found) {
        errorSource.put(
            "name", userGFlag.getKey() + " is not present as per current " + "metadata");
      }
      // set empty object for gflag indicating no error detected.
      errorObject.put(userGFlag.getKey(), errorSource);
    }
    return errorObject;
  }

  private String checkValueType(String inputValue, String expectedType) {
    String errorString = "";
    switch (expectedType) {
      case "bool":
        if (!(inputValue.equals("true") || inputValue.equals("false"))) {
          errorString = "Given string is not a bool type";
        }
        break;
      case "double":
        try {
          Double.parseDouble(inputValue);
        } catch (NumberFormatException e) {
          errorString = "Given string is not a double type";
        }
        break;
      case "int32":
        try {
          Integer.parseInt(inputValue);
        } catch (NumberFormatException e) {
          errorString = "Given string is not a int32 type";
        }
        break;
      case "int64":
        try {
          Long.parseLong(inputValue);
        } catch (NumberFormatException e) {
          errorString = "Given string is not a int64 type";
        }
        break;
      case "uint64":
        try {
          Long.parseUnsignedLong(inputValue);
        } catch (NumberFormatException e) {
          errorString = "Given string is not a uint64 type";
        }
        break;
    }
    return errorString;
  }

  /** Checks the db version format */
  private void validateVersionFormat(String version) throws PlatformServiceException {
    Pattern pattern = Pattern.compile("^((\\d+).(\\d+).(\\d+).(\\d+)(?:-[a-z]+)?(\\d+)?)$");
    Matcher matcher = pattern.matcher(version);
    if (!matcher.matches()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Incorrect version format. Valid formats: 1.1.1.1 or 1.1.1.1-b1");
    }
  }

  /** Checks the input server type */
  private void validateServerType(String serverType) throws PlatformServiceException {
    if (!ImmutableList.of(ServerType.MASTER.toString(), ServerType.TSERVER.toString())
        .contains(serverType)) {
      throw new PlatformServiceException(BAD_REQUEST, "Given server type is not valid");
    }
  }
}
