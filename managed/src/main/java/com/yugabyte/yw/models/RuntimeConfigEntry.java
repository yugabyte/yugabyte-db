package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;
import static play.mvc.Http.Status.NOT_FOUND;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.helpers.CommonUtils;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.Transactional;
import jakarta.persistence.EmbeddedId;
import jakarta.persistence.Entity;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Entity
public class RuntimeConfigEntry extends Model {

  private static final Finder<UUID, RuntimeConfigEntry> find =
      new Finder<UUID, RuntimeConfigEntry>(RuntimeConfigEntry.class) {};

  private static final Logger LOG = LoggerFactory.getLogger(RuntimeConfigEntry.class);
  private static final Set<String> sensitiveKeys =
      ImmutableSet.of("yb.security.ldap.ldap_service_account_password", "yb.security.secret");

  @EmbeddedId private final RuntimeConfigEntryKey idKey;

  private byte[] value;

  public RuntimeConfigEntry(UUID scopedConfigId, String path, String value) {
    this.idKey = new RuntimeConfigEntryKey(scopedConfigId, path);
    this.setValue(value);
  }

  public String getPath() {
    return idKey.getPath();
  }

  public String getValue() {
    return new String(this.value, StandardCharsets.UTF_8);
  }

  public void setValue(String value) {
    this.value = value.getBytes(StandardCharsets.UTF_8);
  }

  private static final Finder<UUID, RuntimeConfigEntry> findInScope =
      new Finder<UUID, RuntimeConfigEntry>(RuntimeConfigEntry.class) {};

  private static final Finder<RuntimeConfigEntryKey, RuntimeConfigEntry> findOne =
      new Finder<RuntimeConfigEntryKey, RuntimeConfigEntry>(RuntimeConfigEntry.class) {};

  public static List<RuntimeConfigEntry> getAll(UUID scope) {
    return findInScope.query().where().eq("scope_uuid", scope).findList();
  }

  @Deprecated
  public static RuntimeConfigEntry get(UUID scope, String path) {
    return findOne.byId(new RuntimeConfigEntryKey(scope, path));
  }

  public static Optional<RuntimeConfigEntry> maybeGet(UUID scope, String path) {
    return RuntimeConfigEntry.find
        .query()
        .where()
        .eq("scope_uuid", scope)
        .eq("path", path)
        .findOneOrEmpty();
  }

  public static List<RuntimeConfigEntry> get(UUID scope, List<String> paths) {
    return RuntimeConfigEntry.find
        .query()
        .where()
        .eq("scope_uuid", scope)
        .in("path", paths)
        .findList();
  }

  public static RuntimeConfigEntry getOrBadRequest(UUID scope, String path) {
    RuntimeConfigEntry runtimeConfigEntry = get(scope, path);
    if (runtimeConfigEntry == null)
      throw new PlatformServiceException(
          NOT_FOUND, String.format("Key %s is not defined in scope %s", path, scope));
    return runtimeConfigEntry;
  }

  public static Map<String, String> getAsMapForScope(UUID scope) {
    List<RuntimeConfigEntry> scopedValues = getAll(scope);
    Map<String, String> map = new HashMap<>();
    for (RuntimeConfigEntry scopedValue : scopedValues) {
      String path = scopedValue.getPath();
      String value = scopedValue.getValue();
      if (path == null || value == null) {
        LOG.warn("Null key or value in runtime config {} = {}", path, value);
        continue;
      }
      if (map.put(path, value) != null) {
        LOG.warn("Duplicate key in runtime config {}", path);
      }
    }
    return map;
  }

  private static RuntimeConfigEntry upsertInternal(
      UUID uuid, String path, String value, Runnable ensure) {
    RuntimeConfigEntry config = get(uuid, path);
    String logValue =
        sensitiveKeys.contains(path) ? CommonUtils.getMaskedValue(path, value) : value;
    LOG.debug("Setting {} value to: {}", path, logValue);

    if (config == null) {
      ensure.run();
      config = new RuntimeConfigEntry(uuid, path, value);
    } else {
      config.setValue(value);
    }

    config.save();
    return config;
  }

  @Transactional
  public static RuntimeConfigEntry upsertGlobal(String path, String value) {
    return upsertInternal(GLOBAL_SCOPE_UUID, path, value, ScopedRuntimeConfig::ensureGlobal);
  }

  @Transactional
  public static RuntimeConfigEntry upsert(Customer customer, String path, String value) {
    return upsertInternal(
        customer.getUuid(), path, value, () -> ScopedRuntimeConfig.ensure(customer));
  }

  @Transactional
  public static RuntimeConfigEntry upsert(Universe universe, String path, String value) {
    return upsertInternal(
        universe.getUniverseUUID(), path, value, () -> ScopedRuntimeConfig.ensure(universe));
  }

  @Transactional
  public static RuntimeConfigEntry upsert(Provider provider, String path, String value) {
    return upsertInternal(
        provider.getUuid(), path, value, () -> ScopedRuntimeConfig.ensure(provider));
  }

  @Override
  public String toString() {
    return "RuntimeConfigEntry{" + "idKey=" + idKey + ", value='" + value + '\'' + '}';
  }
}
