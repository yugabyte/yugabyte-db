package com.yugabyte.troubleshoot.ts.metric.client;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import org.springframework.http.MediaType;
import org.springframework.http.client.ClientHttpResponse;
import org.springframework.stereotype.Component;
import org.springframework.util.ObjectUtils;
import org.springframework.web.client.DefaultResponseErrorHandler;

@Component
public class PrometheusErrorHandler extends DefaultResponseErrorHandler {

  private final ObjectMapper objectMapper;

  public PrometheusErrorHandler(ObjectMapper objectMapper) {
    this.objectMapper = objectMapper;
  }

  @Override
  public void handleError(ClientHttpResponse response) throws IOException {
    byte[] body = getResponseBody(response);
    Charset charset = getCharset(response);

    if (ObjectUtils.isEmpty(body)) {
      throw new PrometheusError(response.getStatusCode());
    }

    charset = (charset != null ? charset : StandardCharsets.UTF_8);
    String bodyText = new String(body, charset);
    MediaType contentType = response.getHeaders().getContentType();
    if (contentType == null) {
      throw new PrometheusError(response.getStatusCode());
    }
    if (contentType.includes(MediaType.APPLICATION_JSON)) {
      JsonNode bodyJson = objectMapper.readTree(bodyText);
      JsonNode errorType = bodyJson.get("errorType");
      JsonNode error = bodyJson.get("error");
      throw new PrometheusError(
          response.getStatusCode(),
          errorType != null ? errorType.asText() : null,
          error != null ? error.asText() : null);
    }
    throw new PrometheusError(response.getStatusCode(), null, bodyText);
  }
}
