package com.yugabyte.yw.models.common;

import com.yugabyte.yw.common.concurrent.KeyLock;
import java.util.concurrent.CompletionStage;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Action;
import play.mvc.Http.Request;
import play.mvc.Result;

@Slf4j
public class SynchronizedControllerAction extends Action<SynchronizedController> {

  private static final KeyLock<String> keyLock = new KeyLock<String>();

  @Override
  public CompletionStage<Result> call(Request req) {
    String key = "";
    String path = req.path();
    path = path.substring(path.indexOf("customers"));
    String[] pathParts = path.split("/");
    KeyFromURI keyFromURI = configuration.keyFromURI();
    for (int index : keyFromURI.pathIndices()) {
      key += pathParts[index];
    }
    for (String param : keyFromURI.queryParams()) {
      key += req.queryString(param).orElse("YB");
    }
    keyLock.acquireLock(key);
    try {
      return delegate.call(req);
    } catch (Exception e) {
      log.error("Failed to synchronize controller for api {}", req.path());
      throw e;
    } finally {
      keyLock.releaseLock(key);
    }
  }
}
