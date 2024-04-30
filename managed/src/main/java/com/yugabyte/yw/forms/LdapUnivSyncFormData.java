// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.LdapUtil.TlsProtocol;
import com.yugabyte.yw.common.Util;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.List;
import lombok.Data;
import play.data.validation.Constraints;

@Data
@ApiModel(description = "Config to sync universe roles with ldap users")
public class LdapUnivSyncFormData {

  public enum TargetApi {
    ysql,
    ycql
  }

  @ApiModelProperty(required = true)
  @Constraints.Required()
  private TargetApi targetApi;

  @ApiModelProperty(value = "Database user to connect: yugabyte for ysql, cassandra for ycql")
  private String dbUser;

  @ApiModelProperty(required = true)
  private String dbuserPassword;

  @ApiModelProperty(value = "IP address of the LDAP server")
  private String ldapServer;

  @ApiModelProperty(value = "Port of the ldap server : 389 or 636(tls)")
  private Integer ldapPort;

  @ApiModelProperty(
      value = "Dn of the user authenticating to LDAP.",
      example = "cn=user,dc=example,dc=com")
  private String ldapBindDn;

  @ApiModelProperty(value = "Password of the user authenticating to LDAP.")
  private String ldapBindPassword;

  @ApiModelProperty(
      value = "LDAP search filter to get the user entries",
      example = "(objectclass=person)")
  private String ldapSearchFilter;

  @ApiModelProperty(value = "Dn of the search starting point.", example = "dc=example,dc=org")
  private String ldapBasedn;

  @ApiModelProperty(value = "LDAP group dn attribute to which the user belongs")
  private String ldapGroupMemberOfAttribute = "memberOf";

  @ApiModelProperty(
      value = "Dn/Attribute field to get the user's name from",
      required = true,
      example = "cn, sAMAccountName")
  @Constraints.Required()
  private String ldapUserfield;

  @ApiModelProperty(
      value = "LDAP field to get the group information",
      required = true,
      example = "cn")
  @Constraints.Required()
  private String ldapGroupfield;

  @ApiModelProperty(value = "Use LDAP TLS")
  private Boolean useLdapTls;

  @ApiModelProperty(value = "Allow the API to create the LDAP groups as DB superusers")
  private Boolean createGroups = false;

  @ApiModelProperty(value = "List of users to exclude while revoking and dropping")
  private List<String> excludeUsers = new ArrayList<>();

  @ApiModelProperty(value = "TLS versions for LDAPS : TLSv1, TLSv1_1, TLSv1_2")
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
}
