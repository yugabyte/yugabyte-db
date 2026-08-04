package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.PlatformServiceException;
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
      cluster.validateProxyConfig(cluster.userIntent, universe.getNodesInCluster(cluster.uuid));
      UserIntent curIntent = universe.getCluster(cluster.uuid).userIntent;
      UserIntent newIntent = cluster.userIntent;
      Map<UUID, ProxyConfig> curProxyOverrides = null;
      Map<UUID, ProxyConfig> newProxyOverrides = null;
      if (curIntent.getUserIntentOverrides() != null) {
        curProxyOverrides = curIntent.getUserIntentOverrides().getAZProxyConfigMap();
      }
      if (newIntent.getUserIntentOverrides() != null) {
        newProxyOverrides = newIntent.getUserIntentOverrides().getAZProxyConfigMap();
      }
      if (ObjectUtils.notEqual(newIntent.getProxyConfig(), curIntent.getProxyConfig())
          || ObjectUtils.notEqual(curProxyOverrides, newProxyOverrides)) {
        changed = true;
      }
    }
    if (!changed) {
      throw new PlatformServiceException(BAD_REQUEST, "No changes made to proxy config");
    }
  }

  public static class Converter extends BaseConverter<ProxyConfigUpdateParams> {}
}
