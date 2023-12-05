// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonIdentityInfo;
import com.fasterxml.jackson.annotation.ObjectIdGenerators;
import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import java.util.List;
import java.util.Map;
import java.util.UUID;
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

  public HookScope getHookScopeOrFail(
      UUID universeUUID, UUID clusterUUID, HookScope.TriggerType triggerType) {
    HookScope hookScope = getHookScope();
    if (hookScope == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Hook: " + uuid + " is not associated to any scope");
    } else if (!hookScope.getTriggerType().equals(triggerType)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Hook: "
              + uuid
              + " is not associated to an "
              + HookScope.TriggerType.ApiTriggered.name()
              + " hook scope");
    } else if (!hookScope.getCustomerUUID().equals(customerUUID)) {
      // Don't want to leak that the hook exists if it isn't associated to this customer somehow.
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Cannot find hook: " + uuid);
    } else if (hookScope.getUniverseUUID() != null
        && !hookScope.getUniverseUUID().equals(universeUUID)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Hook: "
              + uuid
              + " is associated to a hook scope for a different universe: "
              + hookScope.getUniverseUUID());
    } else if (clusterUUID != null
        && hookScope.getClusterUUID() != null
        && !hookScope.getClusterUUID().equals(clusterUUID)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Hook: "
              + uuid
              + " is associated to a hook scope for a different cluster: "
              + hookScope.getClusterUUID());
    }

    return hookScope;
  }

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
