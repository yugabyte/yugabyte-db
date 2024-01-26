// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.LdapUtil.TlsProtocol;
import com.yugabyte.yw.common.Util;
import java.util.ArrayList;
import java.util.List;
import lombok.Data;
import play.data.validation.Constraints;

@Data
public class LdapUnivSyncFormData {

  public enum TargetApi {
    ysql,
    ycql
  }

  @Constraints.Required() private TargetApi targetApi;
  private String dbUser;
  private String dbuserPassword;
  private String ldapServer;
  private Integer ldapPort = 389;
  private String ldapBindDn;
  private String ldapBindPassword;
  private String ldapSearchFilter;
  private String ldapBasedn;
  private String ldapGroupMemberOfAttribute = "memberOf";

  @Constraints.Required() private String ldapUserfield;
  @Constraints.Required() private String ldapGroupfield;

  private Boolean useLdapTls = false;
  private Boolean createGroups = false;
  private Boolean allowDropSuperuser = false;
  private List<String> excludeUsers = new ArrayList<>();
  private TlsProtocol ldapTlsProtocol = TlsProtocol.TLSv1_2;

  public String getDbUser() {
    if (this.dbUser.isEmpty() && this.targetApi.equals(TargetApi.ycql)) {
      return Util.DEFAULT_YCQL_USERNAME;
    } else if (this.dbUser.isEmpty() && this.targetApi.equals(TargetApi.ysql)) {
      return Util.DEFAULT_YSQL_USERNAME;
    }
    return this.dbUser;
  }

  public String getLdapGroupMemberOfAttribute() {
    if (this.ldapGroupMemberOfAttribute.isEmpty()) {
      return "memberOf";
    }
    return this.ldapGroupMemberOfAttribute;
  }

  public Boolean getCreateGroups() {
    if (this.createGroups == null) {
      return false;
    }
    return this.createGroups;
  }

  public Boolean getAllowDropSuperuser() {
    if (this.allowDropSuperuser == null) {
      return false;
    }
    return this.allowDropSuperuser;
  }
}
