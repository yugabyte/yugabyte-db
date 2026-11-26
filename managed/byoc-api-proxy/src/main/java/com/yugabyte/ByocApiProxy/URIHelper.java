package com.yugabyte.ByocApiProxy;

import java.net.URI;

public class URIHelper {
  private URIHelper() {}

  public static URI replaceBaseAndNormalize(String originalUriStr, String newBaseUriStr) {
    URI originalUri = URI.create(originalUriStr);
    URI newBaseUri = URI.create(newBaseUriStr);

    String uri = String.join("/", newBaseUri.resolve("/").toString(), originalUri.getRawPath());
    String rawQuery = originalUri.getRawQuery();

    if (rawQuery != null && !rawQuery.isEmpty()) {
      uri = uri + "?" + rawQuery;
    }

    return URI.create(uri).normalize();
  }
}
