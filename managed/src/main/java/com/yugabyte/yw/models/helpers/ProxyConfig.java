// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.net.URL;
import java.util.List;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.ybc.ProxySpec;

@ApiModel(description = "The Proxy Settings for the Universe")
@Data
@NoArgsConstructor
public class ProxyConfig {
  @ApiModelProperty(value = "The HTTP_PROXY to use", example = "http://10.9.131.7:3128")
  private String httpProxy;

  @ApiModelProperty(value = "The HTTPS_PROXY to use", example = "http://10.9.131.7:3128")
  private String httpsProxy;

  @ApiModelProperty(
      value = "The NO_PROXY settings. Should follow cURL no_proxy format",
      example = "[\"10.150.0.26\",\".amazonaws.com\"]")
  private List<String> noProxyList;

  public void validate() {
    if (StringUtils.isBlank(httpProxy) && StringUtils.isBlank(httpsProxy)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Both httpProxy and httpsProxy cannot be null");
    }
    // URL validation
    if (StringUtils.isNotBlank(httpProxy)) {
      try {
        Util.validateAndGetURL(httpProxy, true);
      } catch (Exception e) {
        throw new PlatformServiceException(BAD_REQUEST, "Bad HTTP_PROXY URL format");
      }
    }
    if (StringUtils.isNotBlank(httpsProxy)) {
      try {
        Util.validateAndGetURL(httpsProxy, true);
      } catch (Exception e) {
        throw new PlatformServiceException(BAD_REQUEST, "Bad HTTPS_PROXY URL format");
      }
    }
    validateNoProxy();
  }

  private void validateNoProxy() {
    // No-op for now.
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    ProxyConfig that = (ProxyConfig) o;
    boolean equalNoProxy = false;
    if (CollectionUtils.isNotEmpty(this.noProxyList)
        && CollectionUtils.isNotEmpty(that.noProxyList)) {
      equalNoProxy = this.noProxyList.equals(that.noProxyList);
    } else if (CollectionUtils.isEmpty(this.noProxyList)
        && CollectionUtils.isEmpty(that.noProxyList)) {
      equalNoProxy = true;
    }
    return StringUtils.equals(this.httpProxy, that.httpProxy)
        && StringUtils.equals(this.httpsProxy, that.httpsProxy)
        && equalNoProxy;
  }

  public ProxyConfig clone() {
    ProxyConfig newProxyConfig = new ProxyConfig();
    newProxyConfig.httpProxy = httpProxy;
    newProxyConfig.httpsProxy = httpsProxy;
    newProxyConfig.noProxyList = noProxyList;
    return newProxyConfig;
  }

  @Data
  @AllArgsConstructor
  public class ProxySetting {
    private String scheme = "http";
    private String proxy;
    private int port = 0;
    private String password;
    private String username;
  }

  @JsonIgnore
  public ProxySetting getProxySetting(boolean useHttpsProxy) {
    String urlString = useHttpsProxy ? httpsProxy : httpProxy;
    if (StringUtils.isBlank(urlString)) {
      return null;
    }
    URL url = Util.validateAndGetURL(urlString, true);
    String scheme = url.getProtocol();
    String host = url.getHost();
    int port = 0;
    if (url.getPort() != -1) {
      port = url.getPort();
    }
    String username = null;
    String password = null;
    if (StringUtils.isNotBlank(url.getUserInfo())) {
      String userInfo = url.getUserInfo();
      if (userInfo.contains(":")) {
        username = userInfo.substring(0, userInfo.indexOf(":"));
        password = userInfo.substring(userInfo.indexOf(":") + 1);
      } else {
        username = userInfo;
      }
    }
    return new ProxySetting(scheme, host, port, password, username);
  }

  @JsonIgnore
  public ProxySpec getYbcProxySpec(boolean useHttpsProxy) {
    ProxySetting ps = getProxySetting(useHttpsProxy);
    ProxySpec.Builder pspecBuilder = ProxySpec.newBuilder();
    pspecBuilder.setHost(ps.getProxy());
    if (ps.getPort() > 0) {
      pspecBuilder.setPort(ps.getPort());
    }
    if (StringUtils.isNotBlank(ps.getUsername())) {
      pspecBuilder.setUsername(ps.getUsername());
    }
    if (StringUtils.isNotBlank(ps.getPassword())) {
      pspecBuilder.setPassword(ps.getPassword());
    }
    pspecBuilder.setScheme(ps.getScheme());
    if (CollectionUtils.isNotEmpty(noProxyList)) {
      pspecBuilder.addAllNoProxy(noProxyList);
    }
    return pspecBuilder.build();
  }
}
