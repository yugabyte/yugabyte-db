/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.LdapUnivSync;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.LdapUnivSyncFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.net.URI;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class LdapUniverseSyncHandler {

  @Inject private Commissioner commissioner;
  public static final String DOUBLE_QUOTED_PATTERN = "\"\"(.*?)\"\"";
  public static final String NO_DQUOTE_PATTERN = "(.*?)(?:\\s|$|\")";
  public static final String DIGIT_PATTERN = "(\\d+)";

  public static final String YCQL_LDAP_SERVER = "ycql_ldap_server";
  public static final String YCQL_LDAP_BIND_DN = "ycql_ldap_bind_dn";
  public static final String YCQL_LDAP_BIND_PASSWD = "ycql_ldap_bind_passwd";
  public static final String YCQL_LDAP_SEARCH_FILTER = "ycql_ldap_search_filter";
  public static final String YCQL_LDAP_BASE_DN = "ycql_ldap_base_dn";
  public static final String YCQL_LDAP_TLS = "ycql_ldap_tls";

  public String validateAndExtractValueFromGFlag(
      String gFlag,
      String extractValue,
      String fieldName,
      boolean isYsql,
      String gFlagKey,
      Map<String, String> tserverMap) {
    String value;
    if (!isYsql) {
      value = tserverMap.get(gFlagKey);
    } else {
      String pattern = "";
      switch (extractValue) {
        case "ldapserver":
        case "ldapbindpasswd":
          pattern = NO_DQUOTE_PATTERN;
          break;
        case "ldaptls":
        case "ldapport":
          pattern = DIGIT_PATTERN;
          break;
        default:
          pattern = DOUBLE_QUOTED_PATTERN;
          break;
      }
      value = Util.extractRegexValue(gFlag, String.format("%s=%s", extractValue, pattern));
    }

    if (value != null || fieldName.equals("ldapPort") || fieldName.equals("useLdapTls")) {
      return value;
    }

    throw new PlatformServiceException(
        BAD_REQUEST,
        String.format(
            "Couldnt extract the value from the gflag. %s(%s) is required.", fieldName, value));
  }

  public String validateAndExtractValueFromGFlag(
      String fieldName, String gFlagKey, Map<String, String> tserverMap) {
    return validateAndExtractValueFromGFlag("", "", fieldName, false, gFlagKey, tserverMap);
  }

  public String validateAndExtractValueFromGFlag(
      String gFlag, String fieldName, String extractValue) {
    return validateAndExtractValueFromGFlag(gFlag, extractValue, fieldName, true, "", null);
  }

  public LdapUnivSyncFormData populateFromGflagYSQL(
      LdapUnivSyncFormData ldapUnivSyncFormData, String gFlag) {

    // fallback to gflag value, if formData has any of these (ldapServer, ldapPort
    // ldapBindDn, ldapPass, ldapSearchFilter, ldapBasedn) values as empty.
    if (StringUtils.isEmpty(ldapUnivSyncFormData.getLdapServer())) {
      ldapUnivSyncFormData.setLdapServer(
          validateAndExtractValueFromGFlag(gFlag, "ldapServer", "ldapserver"));
    }
    if (StringUtils.isEmpty(ldapUnivSyncFormData.getLdapBindDn())) {
      ldapUnivSyncFormData.setLdapBindDn(
          validateAndExtractValueFromGFlag(gFlag, "ldapBindDn", "ldapbinddn"));
    }
    if (StringUtils.isEmpty(ldapUnivSyncFormData.getLdapBindPassword())) {
      ldapUnivSyncFormData.setLdapBindPassword(
          validateAndExtractValueFromGFlag(gFlag, "ldapBindPassword", "ldapbindpasswd"));
    }
    if (StringUtils.isEmpty(ldapUnivSyncFormData.getLdapSearchFilter())) {
      ldapUnivSyncFormData.setLdapSearchFilter(
          validateAndExtractValueFromGFlag(gFlag, "ldapSearchFilter", "ldapsearchfilter"));
    }
    if (StringUtils.isEmpty(ldapUnivSyncFormData.getLdapBasedn())) {
      ldapUnivSyncFormData.setLdapBasedn(
          validateAndExtractValueFromGFlag(gFlag, "ldapBasedn", "ldapbasedn"));
    }
    if (ldapUnivSyncFormData.getUseLdapTls() == null) {
      String tls = validateAndExtractValueFromGFlag(gFlag, "useLdapTls", "ldaptls");
      ldapUnivSyncFormData.setUseLdapTls((tls != null) && tls.equals("1"));
    }
    if (ldapUnivSyncFormData.getLdapPort() == null) {
      String port = validateAndExtractValueFromGFlag(gFlag, "ldapPort", "ldapport");
      if (port == null) {
        // Set the default LDAP port based on whether LDAP TLS is used
        if (ldapUnivSyncFormData.getUseLdapTls()) {
          ldapUnivSyncFormData.setLdapPort(636);
        } else {
          ldapUnivSyncFormData.setLdapPort(389);
        }
      } else {
        ldapUnivSyncFormData.setLdapPort(Integer.valueOf(port));
      }
    }

    return ldapUnivSyncFormData;
  }

  public LdapUnivSyncFormData populateFromGflagYCQL(
      LdapUnivSyncFormData ldapUnivSyncFormData, Map<String, String> tserverMap) {

    // fallback to gflag value, if formData has any of these (ldapServer, ldapPort,
    // ldapBindDn, ldapPass, ldapSearchFilter, ldapBasedn) values as empty.
    if (StringUtils.isEmpty(ldapUnivSyncFormData.getLdapServer())
        || ldapUnivSyncFormData.getLdapPort() == null) {
      String serverIpPort =
          validateAndExtractValueFromGFlag("ldapServer", YCQL_LDAP_SERVER, tserverMap);
      if (serverIpPort != null) {
        // Parse the LDAP URL
        URI uri = null;
        try {
          uri = new URI(serverIpPort);
        } catch (Exception e) {
          log.error("Error populating the gFlag with error='{}'.", e.getMessage(), e);
          String errorMsg =
              String.format("Error populating the gFlag with error=%s. %s", e.getMessage(), e);
          throw new PlatformServiceException(BAD_REQUEST, errorMsg);
        }
        // Get the host (server IP)
        if (StringUtils.isEmpty(ldapUnivSyncFormData.getLdapServer())) {
          ldapUnivSyncFormData.setLdapServer(uri.getHost());
        }
        if (ldapUnivSyncFormData.getLdapPort() == null) {
          ldapUnivSyncFormData.setLdapPort(uri.getPort());
        }
      }
    }

    if (StringUtils.isEmpty(ldapUnivSyncFormData.getLdapBindDn())) {
      ldapUnivSyncFormData.setLdapBindDn(
          validateAndExtractValueFromGFlag("ldapBindDn", YCQL_LDAP_BIND_DN, tserverMap));
    }
    if (StringUtils.isEmpty(ldapUnivSyncFormData.getLdapBindPassword())) {
      ldapUnivSyncFormData.setLdapBindPassword(
          validateAndExtractValueFromGFlag("ldapBindPassword", YCQL_LDAP_BIND_PASSWD, tserverMap));
    }
    if (StringUtils.isEmpty(ldapUnivSyncFormData.getLdapSearchFilter())) {
      ldapUnivSyncFormData.setLdapSearchFilter(
          validateAndExtractValueFromGFlag(
              "ldapSearchFilter", YCQL_LDAP_SEARCH_FILTER, tserverMap));
    }
    if (StringUtils.isEmpty(ldapUnivSyncFormData.getLdapBasedn())) {
      ldapUnivSyncFormData.setLdapBasedn(
          validateAndExtractValueFromGFlag("ldapBasedn", YCQL_LDAP_BASE_DN, tserverMap));
    }
    if (ldapUnivSyncFormData.getUseLdapTls() == null) {
      ldapUnivSyncFormData.setUseLdapTls(
          Boolean.parseBoolean(
              validateAndExtractValueFromGFlag("useLdapTls", YCQL_LDAP_TLS, tserverMap)));
    }
    return ldapUnivSyncFormData;
  }

  private void validateAndPopulateForYsql(
      LdapUnivSyncFormData ldapUnivSyncFormData, String gFlag, String universeName) {
    if (gFlag != null) {
      ldapUnivSyncFormData = populateFromGflagYSQL(ldapUnivSyncFormData, gFlag);
    } else {
      throwBadRequestForUniverse(universeName, "YSQL");
    }
  }

  private void validateAndPopulateForYcql(
      LdapUnivSyncFormData ldapUnivSyncFormData,
      boolean ldapFlag,
      String universeName,
      Map<String, String> tserverMap) {
    if (ldapFlag) {
      ldapUnivSyncFormData = populateFromGflagYCQL(ldapUnivSyncFormData, tserverMap);
    } else {
      throwBadRequestForUniverse(universeName, "YCQL");
    }
  }

  private void throwBadRequestForUniverse(String universeName, String apiType) {
    String errorMsg =
        String.format(
            "LDAP authentication for %s is not enabled on the universe: %s", apiType, universeName);
    throw new PlatformServiceException(BAD_REQUEST, errorMsg);
  }

  /**
   * Handler that performs the actual sync of the universe with the ldap server
   *
   * @param customerUUID
   * @param customer
   * @param universeUUID
   * @param universe
   * @param LdapUnivSyncFormData
   * @throws Exception
   */
  public UUID syncUniv(
      UUID customerUUID,
      Customer customer,
      UUID universeUUID,
      Universe universe,
      LdapUnivSyncFormData ldapUnivSyncFormData) {

    String errorMsg;
    Map<String, String> tserverMap = new HashMap<>();

    try {
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      Cluster primaryCluster = universeDetails.getPrimaryCluster();
      tserverMap =
          primaryCluster
              .userIntent
              .specificGFlags
              .getPerProcessFlags()
              .value
              .getOrDefault(ServerType.TSERVER, tserverMap);
    } catch (Exception e) {
      log.error("Error extracting the gFlag with error='{}'.", e.getMessage(), e);
      errorMsg = String.format("Error extracting the gFlag with error=%s. %s", e.getMessage(), e);
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }

    if (ldapUnivSyncFormData.getTargetApi() == LdapUnivSyncFormData.TargetApi.ysql) {
      String gFlag = tserverMap.get(GFlagsUtil.YSQL_HBA_CONF_CSV);
      validateAndPopulateForYsql(ldapUnivSyncFormData, gFlag, universe.getName());
    } else {
      boolean ldapFlag =
          Boolean.parseBoolean(tserverMap.get(GFlagsUtil.USE_CASSANDRA_AUTHENTICATION));
      validateAndPopulateForYcql(ldapUnivSyncFormData, ldapFlag, universe.getName(), tserverMap);
    }

    TaskType taskType = TaskType.LdapUniverseSync;

    // Submit the task to perform the LDAP-Universe Sync.
    LdapUnivSync.Params taskParams = new LdapUnivSync.Params();
    taskParams.setUniverseUUID(universeUUID);
    taskParams.ldapUnivSyncFormData = ldapUnivSyncFormData;
    UUID taskUUID = commissioner.submit(taskType, taskParams);

    log.info(
        "Submitted Ldap-Universe Sync for {}:{}, task uuid = {}.",
        universeUUID,
        universe.getName(),
        taskUUID);

    // Add this task uuid to the user universe.
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.LdapSync,
        universe.getName());

    log.info(
        "Saved task uuid {} in customer tasks table for universe {}:{}",
        taskUUID,
        universeUUID,
        universe.getName());

    return taskUUID;
  }
}
