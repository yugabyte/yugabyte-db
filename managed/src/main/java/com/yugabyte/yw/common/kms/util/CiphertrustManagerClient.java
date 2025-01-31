package com.yugabyte.yw.common.kms.util;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.PlatformServiceException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.security.KeyStore;
import java.security.SecureRandom;
import java.util.HashMap;
import java.util.Map;
import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class CiphertrustManagerClient {
  public static final String TOKEN_ENDPOINT = "/api/v1/auth/tokens/";
  public static final String KEYS_ENDPOINT = "/api/v1/vault/keys2";
  public static final String ENCRYPT_ENDPOINT = "/api/v1/crypto/encrypt";
  public static final String DECRYPT_ENDPOINT = "/api/v1/crypto/decrypt";
  public static final Map<String, String> keyLabelsToAdd =
      ImmutableMap.of("created_by_yugabyte", "true");

  public String baseUrl;
  public String refreshToken;
  public KeyStore ybaAndJavaKeyStore;
  public String keyName;
  public String jwt;
  public String keyAlgorithm;
  public int keySize;

  public CiphertrustManagerClient(
      String baseUrl,
      String refreshToken,
      KeyStore ybaAndJavaKeyStore,
      String keyName,
      String keyAlgorithm,
      int keySize) {
    this.baseUrl = baseUrl;
    this.refreshToken = refreshToken;
    this.ybaAndJavaKeyStore = ybaAndJavaKeyStore;
    this.keyName = keyName;
    this.jwt = getJwt();
    this.keyAlgorithm = keyAlgorithm;
    this.keySize = keySize;
  }

  public String constructUrl(String endpoint) {
    return StringUtils.stripEnd(baseUrl, "/") + endpoint;
  }

  public String formatHttpResponse(HttpResponse<String> response) {
    return String.format(
        "URI: %s%nStatus Code: %d%nResponse Body:%n%s",
        response.uri(), response.statusCode(), response.body());
  }

  public SSLContext getSslContext() throws Exception {
    TrustManagerFactory trustFactory =
        TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
    trustFactory.init(ybaAndJavaKeyStore);
    TrustManager[] ybaJavaTrustManagers = trustFactory.getTrustManagers();
    SecureRandom secureRandom = new SecureRandom();
    SSLContext sslContext = SSLContext.getInstance("TLS");
    sslContext.init(null, ybaJavaTrustManagers, secureRandom);
    return sslContext;
  }

  public Map<String, Object> sendGetHttpRequestAndGetBody(String apiUrl) {
    Map<String, Object> responseBody = new HashMap<>();
    try {
      // Convert the map to a JSON string
      ObjectMapper objectMapper = new ObjectMapper();

      // Create the HTTP request
      HttpRequest request =
          HttpRequest.newBuilder()
              .uri(URI.create(apiUrl))
              .header("Content-Type", "application/json")
              .header("Authorization", "Bearer " + this.jwt)
              .GET()
              .build();

      // Send the request
      HttpClient client = HttpClient.newBuilder().sslContext(getSslContext()).build();
      HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());

      // Check the response status code
      if (response.statusCode() == 200) {
        // Resource found successfully.
        // Extract the response body.
        responseBody = objectMapper.readValue(response.body(), Map.class);
        return responseBody;
      } else if (response.statusCode() == 404) {
        // Resource not found.
        log.error("Could not find the resource. Response: " + formatHttpResponse(response));
        return null;
      } else {
        log.error(
            "CipherTrust GET request was not successful. Response: "
                + formatHttpResponse(response));
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "CipherTrust GET request was not successful.");
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  public Map<String, Object> sendPostHttpRequestAndGetBody(
      String apiUrl, Map<String, Object> requestBodyMap, boolean withJwt) {
    Map<String, Object> responseBody = new HashMap<>();
    try {
      // Convert the map to a JSON string
      ObjectMapper objectMapper = new ObjectMapper();
      String requestBody = objectMapper.writeValueAsString(requestBodyMap);

      // Create the HTTP request
      HttpRequest request;
      if (withJwt) {
        request =
            HttpRequest.newBuilder()
                .uri(URI.create(apiUrl))
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .header("Authorization", "Bearer " + this.jwt)
                .POST(HttpRequest.BodyPublishers.ofString(requestBody))
                .build();
      } else {
        request =
            HttpRequest.newBuilder()
                .uri(URI.create(apiUrl))
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(requestBody))
                .build();
      }

      // Send the request
      HttpClient client = HttpClient.newBuilder().sslContext(getSslContext()).build();
      HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());

      // Check the response status code
      if (response.statusCode() == 200 || response.statusCode() == 201) {
        // Extract the response body.
        responseBody = objectMapper.readValue(response.body(), Map.class);
        return responseBody;
      } else {
        log.error(
            "CipherTrust POST request was not successful. Response: "
                + formatHttpResponse(response));
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "CipherTrust POST request was not successful.");
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  public String getJwt() {
    // Construct the API URL
    String apiUrl = constructUrl(TOKEN_ENDPOINT);

    String jwt = null;
    try {
      // Prepare the request body
      Map<String, Object> requestBodyMap = new HashMap<>();
      requestBodyMap.put("grant_type", "refresh_token");
      requestBodyMap.put("refresh_token", refreshToken);

      // Send the request and get the response body
      Map<String, Object> responseBody =
          sendPostHttpRequestAndGetBody(apiUrl, requestBodyMap, false);

      if (responseBody.containsKey("jwt")) {
        jwt = responseBody.get("jwt").toString();
        return jwt;
      } else {
        log.error("No JWT present in response body.");
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "No JWT present in response body.");
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  public String createKey() {
    // Construct the API URL
    String apiUrl = constructUrl(KEYS_ENDPOINT);

    try {
      // Prepare the request body
      Map<String, Object> requestBodyMap = new HashMap<>();
      requestBodyMap.put("name", keyName);
      requestBodyMap.put("usageMask", 12);
      requestBodyMap.put("algorithm", this.keyAlgorithm);
      requestBodyMap.put("size", this.keySize);
      requestBodyMap.put("assignSelfAsOwner", true);
      requestBodyMap.put("labels", keyLabelsToAdd);

      // Send the request and get the response body
      Map<String, Object> responseBody =
          sendPostHttpRequestAndGetBody(apiUrl, requestBodyMap, true);

      if (responseBody.containsKey("name")) {
        return responseBody.get("name").toString();
      } else {
        log.error("Create key response body does not contain key name.");
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Create key response body does not contain key name.");
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  public Map<String, Object> getKeyDetails() {
    // Construct the API URL
    String apiUrl = constructUrl(KEYS_ENDPOINT) + "/" + keyName;

    try {
      // Send the request and get the response body
      Map<String, Object> responseBody = sendGetHttpRequestAndGetBody(apiUrl);

      return responseBody;
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  public Map<String, Object> encryptText(String plainText) {
    // Construct the API URL
    String apiUrl = constructUrl(ENCRYPT_ENDPOINT);

    try {
      // Prepare the request body
      Map<String, Object> requestBodyMap = new HashMap<>();
      requestBodyMap.put("type", "name");
      requestBodyMap.put("id", this.keyName);
      requestBodyMap.put("plaintext", plainText);

      // Send the request and get the response body
      Map<String, Object> responseBody =
          sendPostHttpRequestAndGetBody(apiUrl, requestBodyMap, true);

      if (!responseBody.isEmpty()) {
        return responseBody;
      } else {
        log.error("Encrypt text response body is empty.");
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Encrypt text response body is empty.");
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  public String decryptText(Map<String, Object> cipherTextMap) {
    // Construct the API URL
    String apiUrl = constructUrl(DECRYPT_ENDPOINT);

    try {
      // Send the request and get the response body
      Map<String, Object> responseBody = sendPostHttpRequestAndGetBody(apiUrl, cipherTextMap, true);

      if (responseBody.containsKey("plaintext")) {
        return responseBody.get("plaintext").toString();
      } else {
        log.error("Decrypt text response body does not contain plaintext.");
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Decrypt text response body does not contain plaintext.");
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }
}
