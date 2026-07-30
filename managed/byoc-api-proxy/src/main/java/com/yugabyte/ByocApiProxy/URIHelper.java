package com.yugabyte.ByocApiProxy;

import java.net.URI;

public class URIHelper {
  private URIHelper() {}

  public static URI replaceBaseAndNormalize(String originalUriStr, String newBaseUriStr) {
    URI originalUri = URI.create(originalUriStr);
    URI newBaseUri = URI.create(newBaseUriStr);

    return URI.create(String.join("/", newBaseUri.resolve("/").toString(), originalUri.getPath()))
        .normalize();
  }
}
