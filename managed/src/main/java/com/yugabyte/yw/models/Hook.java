// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIdentityInfo;
import com.fasterxml.jackson.annotation.ObjectIdGenerators;
import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.JoinColumn;
import javax.persistence.ManyToOne;
import lombok.Getter;
import lombok.Setter;

@JsonIdentityInfo(generator = ObjectIdGenerators.PropertyGenerator.class, property = "uuid")
@Entity
@ApiModel(description = "A custom hook.")
@Getter
@Setter
public class Hook extends Model {

  public enum ExecutionLang {
    @EnumValue("Python")
    Python,

    @EnumValue("Bash")
    Bash;
  };

  @Id
  @Column(nullable = false, unique = true)
  @ApiModelProperty(value = "Hook UUID", accessMode = READ_ONLY)
  private UUID uuid = UUID.randomUUID();

  @Column(nullable = false)
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @Column(length = 100, nullable = false)
  @ApiModelProperty(value = "Hook name", required = true)
  private String name;

  @Column(nullable = false)
  @ApiModelProperty(value = "Execution Language", required = true)
  private ExecutionLang executionLang;

  @Column(nullable = false)
  @ApiModelProperty(value = "Hook text", required = true)
  private String hookText;

  @Column(nullable = false)
  @ApiModelProperty(value = "Use superuser privileges", required = true)
  private boolean useSudo;

  @Column(nullable = true)
  @ApiModelProperty(value = "Runtime arguments", required = false)
  @DbJson
  private Map<String, String> runtimeArgs;

  @ManyToOne
  @JoinColumn(name = "hook_scope_uuid", nullable = true)
  @ApiModelProperty(value = "Hook scope", accessMode = READ_ONLY)
  private HookScope hookScope;

  public static Hook create(
      UUID customerUUID,
      String name,
      ExecutionLang executionLang,
      String hookText,
      boolean useSudo,
      Map<String, String> runtimeArgs) {
    Hook hook = new Hook();
    hook.setCustomerUUID(customerUUID);
    hook.setName(name);
    hook.setExecutionLang(executionLang);
    hook.setHookText(hookText);
    hook.setUseSudo(useSudo);
    hook.setRuntimeArgs(runtimeArgs);
    hook.save();
    return hook;
  }

  public static final Finder<UUID, Hook> find = new Finder<UUID, Hook>(Hook.class) {};

  public static Hook getOrBadRequest(UUID customerUUID, UUID hookUUID) {
    Hook hook =
        find.query().where().eq("customer_uuid", customerUUID).eq("uuid", hookUUID).findOne();
    if (hook == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Hook UUID:" + hookUUID);
    }
    return hook;
  }

  public static List<Hook> getAll(UUID customerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).findList();
  }

  public static Hook getByName(UUID customerUUID, String name) {
    return find.query().where().eq("name", name).eq("customer_uuid", customerUUID).findOne();
  }
}
