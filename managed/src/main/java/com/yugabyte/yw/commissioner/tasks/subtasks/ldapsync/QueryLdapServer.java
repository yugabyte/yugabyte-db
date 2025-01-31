package com.yugabyte.yw.commissioner.tasks.subtasks.ldapsync;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.LdapUnivSync.Params;
import com.yugabyte.yw.common.LdapUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.forms.LdapUnivSyncFormData;
import java.util.ArrayList;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.directory.api.ldap.model.cursor.SearchCursor;
import org.apache.directory.api.ldap.model.entry.Attribute;
import org.apache.directory.api.ldap.model.entry.Entry;
import org.apache.directory.api.ldap.model.entry.Value;
import org.apache.directory.api.ldap.model.message.SearchRequest;
import org.apache.directory.api.ldap.model.message.SearchRequestImpl;
import org.apache.directory.api.ldap.model.message.SearchResultDone;
import org.apache.directory.api.ldap.model.message.SearchScope;
import org.apache.directory.api.ldap.model.message.controls.PagedResults;
import org.apache.directory.api.ldap.model.message.controls.PagedResultsImpl;
import org.apache.directory.api.ldap.model.name.Dn;
import org.apache.directory.ldap.client.api.LdapNetworkConnection;

@Slf4j
public class QueryLdapServer extends AbstractTaskBase {
  private final LdapUtil ldapUtil;

  @Inject
  protected QueryLdapServer(BaseTaskDependencies baseTaskDependencies, LdapUtil ldapUtil) {
    super(baseTaskDependencies);
    this.ldapUtil = ldapUtil;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  // extract the userfield or the groupfield
  private String retrieveValueFromDN(String dn, String attribute) {
    String[] attributeValuePairs = dn.split(",");
    for (String attributeValuePair : attributeValuePairs) {
      String[] attributeValue = attributeValuePair.split("=");
      if (attributeValue.length == 2 && attributeValue[0].trim().equalsIgnoreCase(attribute)) {
        return attributeValue[1].trim();
      }
    }
    return "";
  }

  // query the LDAP server, extract user and group data, and organize it into a user-to-group
  // mapping.
  private void queryLdap(LdapNetworkConnection connection, boolean enabledDetailedLogs)
      throws Exception {
    LdapUnivSyncFormData ldapUnivSyncFormData = taskParams().ldapUnivSyncFormData;
    byte[] cookie = null;
    Integer ldapQueryPageSize = confGetter.getGlobalConf(GlobalConfKeys.ldapPageQuerySize);

    do {
      // Setup the paged results control
      PagedResults pagedResultsControl = new PagedResultsImpl();
      pagedResultsControl.setSize(ldapQueryPageSize); // Adjust page size as needed
      pagedResultsControl.setCookie(cookie);

      SearchRequest searchRequest = new SearchRequestImpl();
      searchRequest.setBase(new Dn(ldapUnivSyncFormData.getLdapBasedn()));
      searchRequest.setFilter(ldapUnivSyncFormData.getLdapSearchFilter());
      searchRequest.setScope(SearchScope.SUBTREE);
      searchRequest.addAttributes("*", "+");
      searchRequest.addControl(pagedResultsControl);

      // Execute the search
      SearchCursor cursor = connection.search(searchRequest);

      while (cursor.next()) {
        // Retrieve the entry from the cursor
        Entry entry = cursor.getEntry();

        if (enabledDetailedLogs) {
          log.debug("LDAP user entry retrieved: {}", entry.toString());
        }

        // Process the entry's DN and attributes
        String dn = entry.getDn().toString();
        String userKey = retrieveValueFromDN(dn, ldapUnivSyncFormData.getLdapUserfield());

        if (StringUtils.isEmpty(userKey)) {
          ArrayList<Attribute> userAttributes = new ArrayList<>(entry.getAttributes());
          if (enabledDetailedLogs) {
            log.debug("Number of attributes retrieved: " + userAttributes.size());
            log.debug(
                "User dn {} does not contain {}(userfield). Fetching user attributes...",
                dn,
                ldapUnivSyncFormData.getLdapUserfield());
          }

          // If userKey not found in DN, search in the attributes
          for (Attribute attribute : userAttributes) {
            if (attribute.getId().equalsIgnoreCase(ldapUnivSyncFormData.getLdapUserfield())) {
              userKey = attribute.getString();
              if (enabledDetailedLogs) {
                log.debug("Iterating attribute here: " + attribute);
                log.debug(
                    "User name: {} retrieved from user attribute: {}", userKey, attribute.getId());
              }
            }
          }
        }

        if (enabledDetailedLogs && StringUtils.isEmpty(userKey)) {
          log.warn(
              "User {} does not contain '{}'(userfield). Skipping the user from the sync...",
              dn,
              ldapUnivSyncFormData.getLdapUserfield());
        }

        // Process groups
        if (!StringUtils.isEmpty(userKey)) {
          Attribute groups = entry.get(ldapUnivSyncFormData.getLdapGroupMemberOfAttribute());
          List<String> groupKeys = new ArrayList<>();
          if (groups != null) {
            for (Value group : groups) {
              String groupKey =
                  retrieveValueFromDN(group.getString(), ldapUnivSyncFormData.getLdapGroupfield());
              if (ldapUnivSyncFormData.getGroupsToSync().size() == 0
                  || ldapUnivSyncFormData.getGroupsToSync().contains(groupKey)) {
                groupKeys.add(groupKey);

                if (!taskParams().ldapGroups.contains(groupKey)) {
                  taskParams().ldapGroups.add(groupKey);
                }
              }
            }
          }
          taskParams().userToGroup.put(userKey, groupKeys);
        }
      }

      // Retrieve the PagedResultsControl from the SearchResultDone
      SearchResultDone searchResultDone = cursor.getSearchResultDone();
      PagedResultsImpl responseControl =
          (PagedResultsImpl) searchResultDone.getControl(PagedResultsImpl.OID);
      cookie = (responseControl != null) ? responseControl.getCookie() : null;

      cursor.close();

    } while (cookie != null && cookie.length > 0); // Continue pagination if cookie exists
  }

  @Override
  public void run() {
    log.info("Started {} sub-task for uuid={}", getName(), taskParams().getUniverseUUID());
    LdapNetworkConnection connection = null;
    LdapUnivSyncFormData ldapUnivSyncFormData = taskParams().ldapUnivSyncFormData;

    try {
      // setup ldap connection
      connection =
          ldapUtil.createConnection(
              ldapUnivSyncFormData.getLdapServer(),
              ldapUnivSyncFormData.getLdapPort(),
              ldapUnivSyncFormData.getUseLdapSsl(),
              ldapUnivSyncFormData.getUseLdapTls(),
              ldapUnivSyncFormData.getLdapTlsProtocol().getVersionString());

      connection.bind(
          ldapUnivSyncFormData.getLdapBindDn(), ldapUnivSyncFormData.getLdapBindPassword());
      boolean enableDetailedLogs = confGetter.getGlobalConf(GlobalConfKeys.enableDetailedLogs);
      if (enableDetailedLogs) {
        log.debug(
            "Binding to LDAP Server with distinguishedName: {} and password: {}",
            ldapUnivSyncFormData.getLdapBindDn(),
            "********");
      }

      // query ldap
      queryLdap(connection, enableDetailedLogs);
      if (enableDetailedLogs) {
        log.debug("[LDAP state] groups: {}", taskParams().ldapGroups);
        log.debug("[LDAP state] user-to-group mapping: {}", taskParams().userToGroup);
      }
    } catch (Exception e) {
      log.error("Error connecting to the LDAP Server with error='{}'.", e.getMessage(), e);

      String errorMsg =
          String.format(
              "Error connectign to the LDAP Server with error= %s. %s", e.getMessage(), e);
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    } finally {
      if (connection != null) {
        connection.close();
      }
    }
  }
}
