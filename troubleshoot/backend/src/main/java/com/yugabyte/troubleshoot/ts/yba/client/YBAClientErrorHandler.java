package com.yugabyte.troubleshoot.ts.yba.client;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.TextNode;
import java.io.IOException;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import org.springframework.http.MediaType;
import org.springframework.http.client.ClientHttpResponse;
import org.springframework.stereotype.Component;
import org.springframework.util.ObjectUtils;
import org.springframework.web.client.DefaultResponseErrorHandler;

@Component
public class YBAClientErrorHandler extends DefaultResponseErrorHandler {

  private final ObjectMapper objectMapper;

  public YBAClientErrorHandler(ObjectMapper objectMapper) {
    this.objectMapper = objectMapper;
  }

  @Override
  public void handleError(ClientHttpResponse response) throws IOException {
    byte[] body = getResponseBody(response);
    Charset charset = getCharset(response);

    if (ObjectUtils.isEmpty(body)) {
      throw new YBAClientError(response.getStatusCode());
    }

    charset = (charset != null ? charset : StandardCharsets.UTF_8);
    String bodyText = new String(body, charset);
    MediaType contentType = response.getHeaders().getContentType();
    if (contentType == null) {
      throw new YBAClientError(response.getStatusCode());
    }
    if (contentType.includes(MediaType.APPLICATION_JSON)) {
      JsonNode bodyJson = objectMapper.readTree(bodyText);

      if (!bodyJson.has("error")) {
        throw new YBAClientError(response.getStatusCode());
      }
      throw new YBAClientError(response.getStatusCode(), bodyJson.get("error"));
    }
    throw new YBAClientError(response.getStatusCode(), new TextNode(bodyText));
  }
}
