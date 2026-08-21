package com.yugabyte.ByocApiProxy;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.net.URI;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;

class URIHelperTest {

  @ParameterizedTest
  @CsvSource({
    "http://old-host/api/v1/users, http://new-host:8080, http://new-host:8080/api/v1/users",
    "http://old-host/api/v1/users, http://new-host:8080/, http://new-host:8080/api/v1/users",
    "https://yba.example.com/api/v1/universe, http://localhost:9090, http://localhost:9090/api/v1/universe",
    "http://old-host/api/v1/users?foo=bar, http://new-host:8080, http://new-host:8080/api/v1/users",
    "http://old-host/api/v1/users#section, http://new-host:8080, http://new-host:8080/api/v1/users",
    "http://old-host, http://new-host:8080, http://new-host:8080/",
    "http://old-host/, http://new-host:8080, http://new-host:8080/",
    "http://old-host/api/a/../b, http://new-host:8080, http://new-host:8080/api/b",
    "http://old-host/api//v1/users, http://new-host:8080, http://new-host:8080/api/v1/users",
    "http://old-host/api/v1/users, http://new-host:8080/api-prefix, http://new-host:8080/api/v1/users",
  })
  void replaceBaseAndNormalize_replacesHostAndKeepsPath(
      String originalUri, String newBaseUri, String expectedUri) {
    assertEquals(
        URI.create(expectedUri), URIHelper.replaceBaseAndNormalize(originalUri, newBaseUri));
  }

  @Test
  void replaceBaseAndNormalize_invalidOriginalUri_throws() {
    assertThrows(
        IllegalArgumentException.class,
        () -> URIHelper.replaceBaseAndNormalize("not a uri", "http://new-host:8080"));
  }

  @Test
  void replaceBaseAndNormalize_invalidNewBaseUri_throws() {
    assertThrows(
        IllegalArgumentException.class,
        () -> URIHelper.replaceBaseAndNormalize("http://old-host/path", "not a uri"));
  }
}
