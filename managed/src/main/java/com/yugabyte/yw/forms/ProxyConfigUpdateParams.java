package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.ProxyConfig;
import java.util.Map;
import java.util.UUID;
import org.apache.commons.lang3.ObjectUtils;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = ProxyConfigUpdateParams.Converter.class)
public class ProxyConfigUpdateParams extends UpgradeTaskParams {

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);
    boolean changed = false;
    for (Cluster cluster : this.clusters) {
      Util.validateSpecificationsIfPresent(cluster.userIntent.providerSpecifications, true);
      cluster.validateProxyConfig(cluster.userIntent, universe.getNodesInCluster(cluster.uuid));
      UserIntent curIntent = universe.getCluster(cluster.uuid).userIntent;
      UserIntent newIntent = cluster.userIntent;
      Map<UUID, ProxyConfig> curProxyOverrides = curIntent.getAZProxyConfigMap();
      Map<UUID, ProxyConfig> newProxyOverrides = newIntent.getAZProxyConfigMap();
      if (areBaseProxyConfigsDifferent(curIntent, newIntent)
          || ObjectUtils.notEqual(curProxyOverrides, newProxyOverrides)) {
        changed = true;
      }
    }
    if (!changed) {
      throw new PlatformServiceException(BAD_REQUEST, "No changes made to proxy config");
    }
  }

  private boolean areBaseProxyConfigsDifferent(UserIntent curIntent, UserIntent newIntent) {
    if (curIntent.isMulticloudSupport()) {
      return ObjectUtils.notEqual(
          newIntent.getProviderProxyConfigs(), curIntent.getProviderProxyConfigs());
    }
    return ObjectUtils.notEqual(newIntent.getProxyConfig(), curIntent.getProxyConfig());
  }

  public static class Converter extends BaseConverter<ProxyConfigUpdateParams> {}
}
