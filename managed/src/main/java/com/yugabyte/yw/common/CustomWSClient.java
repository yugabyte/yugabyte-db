package com.yugabyte.yw.common;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;
import lombok.Getter;
import play.libs.ws.WSClient;
import play.libs.ws.WSRequest;
import play.libs.ws.WSRequestFilter;
import play.libs.ws.ahc.AhcWSRequest;

public class CustomWSClient implements WSClient {
  @Getter private final Long id;
  private final WSClient delegate;

  private final Consumer<CustomWSClient> onClose;
  private final List<WSRequestFilter> requestFilterList = new ArrayList<>();

  public CustomWSClient(Long id, WSClient delegate, Consumer<CustomWSClient> onClose) {
    this.id = id;
    this.delegate = delegate;
    this.onClose = onClose;
  }

  @Override
  public Object getUnderlying() {
    return delegate.getUnderlying();
  }

  @Override
  public play.api.libs.ws.WSClient asScala() {
    return delegate.asScala();
  }

  @Override
  public WSRequest url(String url) {
    WSRequest request = delegate.url(url);
    if (request instanceof AhcWSRequest) {
      requestFilterList.forEach(filter -> ((AhcWSRequest) request).setRequestFilter(filter));
    }
    return request;
  }

  public void addFilter(WSRequestFilter filter) {
    requestFilterList.add(filter);
  }

  @Override
  public void close() throws IOException {
    onClose.accept(this);
    delegate.close();
  }
}
