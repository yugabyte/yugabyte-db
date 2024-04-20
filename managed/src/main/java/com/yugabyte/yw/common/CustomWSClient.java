package com.yugabyte.yw.common;

import java.io.IOException;
import java.util.function.Consumer;
import lombok.Getter;
import play.libs.ws.WSClient;
import play.libs.ws.WSRequest;

public class CustomWSClient implements WSClient {
  @Getter private final Long id;
  private final WSClient delegate;

  private final Consumer<CustomWSClient> onClose;

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
    return delegate.url(url);
  }

  @Override
  public void close() throws IOException {
    onClose.accept(this);
    delegate.close();
  }
}
