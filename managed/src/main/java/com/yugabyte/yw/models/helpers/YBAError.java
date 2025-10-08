// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonValue;
import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.DeserializationContext;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.fasterxml.jackson.databind.deser.std.StdDeserializer;
import io.swagger.annotations.ApiModelProperty;
import java.io.IOException;
import java.util.Objects;
import lombok.EqualsAndHashCode;
import lombok.Getter;

@Getter
@EqualsAndHashCode
/** YBA error for internal operations. */
public class YBAError {
  @ApiModelProperty(value = "Error code", accessMode = READ_ONLY)
  private Code code;

  @ApiModelProperty(value = "Error message", accessMode = READ_ONLY)
  private String message;

  @ApiModelProperty(value = "Origin error message", accessMode = READ_ONLY)
  private String originMessage;

  public YBAError(Code code, String message) {
    this(code, message, message);
  }

  @JsonCreator
  public YBAError(
      @JsonProperty("code") Code code,
      @JsonProperty("message") String message,
      @JsonProperty("originMessage") String originMessage) {
    this.code = Objects.requireNonNull(code);
    this.message = Objects.requireNonNull(message);
    this.originMessage = originMessage;
  }

  /** Define all the task error codes here. */
  @JsonDeserialize(using = CodeDeserializer.class)
  public static enum Code {
    UNKNOWN_ERROR,
    INTERNAL_ERROR,
    PLATFORM_SHUTDOWN,
    PLATFORM_RESTARTED,
    INSTALLATION_ERROR,
    SERVICE_START_ERROR,
    CONNECTION_ERROR,
    TIMED_OUT;

    @JsonValue
    public String serialize() {
      return name();
    }
  }

  @SuppressWarnings("serial")
  /** Deserializer does not fail if there are unknown enum constants. */
  public static class CodeDeserializer extends StdDeserializer<Code> {

    public CodeDeserializer() {
      super(Code.class);
    }

    @Override
    public Code deserialize(JsonParser jsonParser, DeserializationContext ctxt)
        throws IOException, JsonProcessingException {
      JsonNode node = jsonParser.getCodec().readTree(jsonParser);
      for (Code code : Code.values()) {
        if (code.name().equalsIgnoreCase(node.asText())) {
          return code;
        }
      }
      return Code.UNKNOWN_ERROR;
    }
  }
}
