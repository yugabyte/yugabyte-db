// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static java.util.regex.Pattern.CASE_INSENSITIVE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.GFlagDetails;
import com.yugabyte.yw.common.gflags.GFlagGroup;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.forms.GFlagsValidationFormData;
import com.yugabyte.yw.forms.GFlagsValidationFormData.GFlagValidationDetails;
import com.yugabyte.yw.forms.GFlagsValidationFormData.GFlagsValidationRequest;
import com.yugabyte.yw.forms.GFlagsValidationFormData.GFlagsValidationResponse;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.function.Function;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class GFlagsValidationHandler {

  @Inject private GFlagsValidation gflagsValidation;

  public static final Logger LOG = LoggerFactory.getLogger(GFlagsValidationHandler.class);

  public static final Set<String> GFLAGS_FILTER_TAGS =
      ImmutableSet.of("experimental", "hidden", "auto");

  public static final Set<String> GFLAGS_FILTER_TAGS_ALLOW_EXPERIMENTAL =
      ImmutableSet.of("hidden", "auto");

  public static final Set<Pattern> GFLAGS_FILTER_PATTERN =
      ImmutableSet.of(
          Pattern.compile("^.*_test.*$", CASE_INSENSITIVE),
          Pattern.compile("^.*test_.*$", CASE_INSENSITIVE));

  private static final Map<String, List<String>> TSERVER_GFLAG_PERMISSIBLE_VALUE =
      ImmutableMap.<String, List<String>>builder()
          .put(
              "ysql_default_transaction_isolation",
              ImmutableList.of("serializable", "read committed", "read repeatable"))
          .build();

  private static final Map<String, List<String>> MASTER_GFLAG_PERMISSIBLE_VALUE =
      ImmutableMap.<String, List<String>>builder().build();

  private static final Map<ServerType, Map<String, List<String>>>
      GFLAGS_PERMISSIBLE_VALUES_PER_SERVER =
          ImmutableMap.<ServerType, Map<String, List<String>>>builder()
              .put(ServerType.MASTER, MASTER_GFLAG_PERMISSIBLE_VALUE)
              .put(ServerType.TSERVER, TSERVER_GFLAG_PERMISSIBLE_VALUE)
              .build();

  public List<GFlagDetails> listGFlags(
      String version,
      String gflag,
      String serverType,
      Boolean mostUsedGFlags,
      Boolean showExperimental)
      throws IOException {
    validateServerType(serverType);
    validateVersionFormat(version);
    List<GFlagDetails> gflagsList =
        gflagsValidation.extractGFlags(version, serverType, mostUsedGFlags);
    gflagsList = filterGFlagsList(gflagsList, showExperimental);
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

  public List<GFlagsValidationResponse> validateGFlags(
      String version, GFlagsValidationFormData gflags) throws IOException {
    validateVersionFormat(version);

    if (gflags.gflagsList == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Please provide a valid list of gflags.");
    }

    // extract master gflags metadata.
    Map<String, GFlagDetails> masterGflagsMap =
        gflagsValidation.extractGFlags(version, ServerType.MASTER.toString(), false).stream()
            .collect(Collectors.toMap(gflag -> gflag.name, Function.identity()));
    // extract tserver gflags metadata.
    Map<String, GFlagDetails> tserverGflagsMap =
        gflagsValidation.extractGFlags(version, ServerType.TSERVER.toString(), false).stream()
            .collect(Collectors.toMap(gflag -> gflag.name, Function.identity()));

    List<String> allowedPreviewMasterFlags = new ArrayList<>();
    List<String> allowedPreviewTServerFlags = new ArrayList<>();
    if (gflags.gflagsList.size() > 0) {
      Optional<GFlagsValidationRequest> allowedPreviewFlagValidationRequest =
          gflags.gflagsList.stream()
              .filter(flag -> flag.name.equals(GFlagsUtil.ALLOWED_PREVIEW_FLAGS_CSV))
              .findFirst();
      if (allowedPreviewFlagValidationRequest.isPresent()) {
        if (allowedPreviewFlagValidationRequest.get().masterValue != null) {
          allowedPreviewMasterFlags =
              ImmutableList.copyOf(
                  Arrays.stream(allowedPreviewFlagValidationRequest.get().masterValue.split(","))
                      .map(String::trim)
                      .collect(Collectors.toList()));
        }
        if (allowedPreviewFlagValidationRequest.get().tserverValue != null) {
          allowedPreviewTServerFlags =
              ImmutableList.copyOf(
                  Arrays.stream(allowedPreviewFlagValidationRequest.get().tserverValue.split(","))
                      .map(String::trim)
                      .collect(Collectors.toList()));
        }
      }
    }

    List<GFlagsValidationResponse> validationResponseArrayList = new ArrayList<>();
    for (GFlagsValidationRequest gflag : gflags.gflagsList) {
      GFlagsValidationResponse validationResponse = new GFlagsValidationResponse();
      validationResponse.name = gflag.name;
      validationResponse.masterResponse =
          checkGflags(gflag, ServerType.MASTER, masterGflagsMap, allowedPreviewMasterFlags);
      validationResponse.tserverResponse =
          checkGflags(gflag, ServerType.TSERVER, tserverGflagsMap, allowedPreviewTServerFlags);
      validationResponseArrayList.add(validationResponse);
    }
    return validationResponseArrayList;
  }

  public GFlagDetails getGFlagsMetadata(String version, String serverType, String gflag)
      throws IOException {
    validateServerType(serverType);
    validateVersionFormat(version);
    List<GFlagDetails> gflagsList = gflagsValidation.extractGFlags(version, serverType, false);
    return gflagsList.stream()
        .filter(flag -> flag.name.equals(gflag))
        .findFirst()
        .orElseThrow(
            () ->
                new PlatformServiceException(BAD_REQUEST, gflag + " is not present in metadata."));
  }

  public List<GFlagGroup> getGFlagGroups(String version, String gflagGroup) throws IOException {
    validateVersionFormat(version);
    List<GFlagGroup> gflagGroupsList = gflagsValidation.extractGFlagGroups(version);
    if (StringUtils.isEmpty(gflagGroup)) {
      return gflagGroupsList;
    }
    List<GFlagGroup> result = new ArrayList<>();
    for (GFlagGroup group : gflagGroupsList) {
      if (group.groupName.toString().contains(gflagGroup)) {
        result.add(group);
      }
    }
    if (result.isEmpty()) {
      throw new PlatformServiceException(BAD_REQUEST, "Unknown gflag group: " + gflagGroup);
    }
    return result;
  }

  private GFlagValidationDetails checkGflags(
      GFlagsValidationRequest gflag,
      ServerType serverType,
      Map<String, GFlagDetails> gflags,
      List<String> allowedPreviewFlags) {
    GFlagValidationDetails validationDetails = new GFlagValidationDetails();
    GFlagDetails gflagDetails = gflags.get(gflag.name);
    if (gflagDetails != null) {
      validationDetails.exist = true;
      String gflagValue = serverType == ServerType.MASTER ? gflag.masterValue : gflag.tserverValue;
      validationDetails.error = checkValueType(gflagValue, gflagDetails.type);
      if (StringUtils.isEmpty(validationDetails.error)) {
        if (GFLAGS_PERMISSIBLE_VALUES_PER_SERVER.get(serverType).containsKey(gflag.name)
            && !GFLAGS_PERMISSIBLE_VALUES_PER_SERVER
                .get(serverType)
                .get(gflag.name)
                .contains(gflagValue)) {
          validationDetails.error = "Given value is not valid";
        }
      }
      if (gflagDetails.tags != null && gflagDetails.tags.contains("preview")) {
        if (!allowedPreviewFlags.contains(gflag.name)) {
          validationDetails.error =
              String.format(
                  "Given flag '%s' is not allowed to be set unless it is added in '%s' flag",
                  gflag.name, GFlagsUtil.ALLOWED_PREVIEW_FLAGS_CSV);
        }
      }
    }
    return validationDetails;
  }

  private String checkValueType(String inputValue, String expectedType) {
    String errorString = null;
    if (inputValue == null) {
      return errorString;
    }
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
    Pattern pattern = Pattern.compile("^((\\d+).(\\d+).(\\d+).(\\d+)(?:-[a-zA-Z0-9]+)*)$");
    Matcher matcher = pattern.matcher(version);
    if (!matcher.matches()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Incorrect version format. Valid formats: 1.1.1.1, 1.1.1.1-b1 or 1.1.1.1-b12-remote");
    }
  }

  /** Checks the input server type */
  private void validateServerType(String serverType) throws PlatformServiceException {
    if (!ImmutableList.of(ServerType.MASTER.toString(), ServerType.TSERVER.toString())
        .contains(serverType)) {
      throw new PlatformServiceException(BAD_REQUEST, "Given server type is not valid");
    }
  }

  private List<GFlagDetails> filterGFlagsList(
      List<GFlagDetails> gflagsList, Boolean showExperimental) {
    return gflagsList.stream()
        .filter(
            flag ->
                !GFLAGS_FILTER_PATTERN.stream()
                        .anyMatch(
                            regexMatcher ->
                                !StringUtils.isEmpty(flag.name)
                                    && regexMatcher.matcher(flag.name).find())
                    && !(showExperimental
                            ? GFLAGS_FILTER_TAGS_ALLOW_EXPERIMENTAL.stream()
                            : GFLAGS_FILTER_TAGS.stream())
                        .anyMatch(
                            tags -> !StringUtils.isEmpty(flag.tags) && flag.tags.contains(tags))
                    && !GFlagsUtil.GFLAGS_FORBIDDEN_TO_OVERRIDE.contains(flag.name))
        .collect(Collectors.toList());
  }
}
