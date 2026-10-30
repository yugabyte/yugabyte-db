// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.cloud.oci;

import com.oracle.bmc.ServiceDetails;
import com.oracle.bmc.model.BmcException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.exception.ExceptionUtils;

/**
 * Builds user-facing messages for OCI SDK errors.
 *
 * <p>{@link BmcException#getMessage()} wraps the service's message in SDK boilerplate (timestamp,
 * client version, request URL, documentation links). The formatted message keeps everything the
 * service returned (message, detail, status, error code), plus the root cause for errors raised
 * before a response arrived, and drops the boilerplate. {@link #logAndShorten} also logs the full
 * exception whenever it shortens it.
 */
@Slf4j
public final class OCIErrorFormatter {

  private static final String UNKNOWN_PREFIX = "Unknown ";

  private OCIErrorFormatter() {}

  /**
   * Returns {@code t.getMessage()} unless an OCI {@link BmcException} is in the cause chain, in
   * which case the OCI error is formatted and combined with any message the wrappers added. Does
   * not log, so callers must log {@code t} themselves.
   */
  public static String userMessage(Throwable t) {
    return shorten(t, false /* logFullError */, null /* context */);
  }

  /** Same as {@link #userMessage(Throwable)}, prefixed with what YBA was doing. */
  public static String userMessage(String context, Throwable t) {
    return withContext(context, t, false /* logFullError */);
  }

  /** Whether {@code t} or one of its causes is an OCI SDK {@link BmcException}. */
  public static boolean isOciError(Throwable t) {
    return indexOfBmcException(ExceptionUtils.getThrowableList(t)) >= 0;
  }

  /**
   * Same as {@link #userMessage(Throwable)}, and logs the full exception when it is shortened.
   *
   * <p>The logging is here rather than in callers because most callers throw a {@code
   * PlatformServiceException}, which keeps no cause, so this log is the only record of the full SDK
   * error and its opc-request-id. An error that an outer caller formats again is not logged twice.
   */
  public static String logAndShorten(Throwable t) {
    return shorten(t, true /* logFullError */, null /* context */);
  }

  /** Same as {@link #logAndShorten(Throwable)}, prefixed with what YBA was doing. */
  public static String logAndShorten(String context, Throwable t) {
    return withContext(context, t, true /* logFullError */);
  }

  private static String withContext(String context, Throwable t, boolean logFullError) {
    if (!isOciError(t)) {
      // Callers expect non-OCI exceptions in the plain "<context>: <message>" form.
      return context + ": " + t.getMessage();
    }
    return withContext(context, shorten(t, logFullError, context));
  }

  private static String shorten(Throwable t, boolean logFullError, String context) {
    List<Throwable> chain = ExceptionUtils.getThrowableList(t);
    int bmcIndex = indexOfBmcException(chain);
    if (bmcIndex < 0) {
      return t.getMessage();
    }
    BmcException bmc = (BmcException) chain.get(bmcIndex);
    String formatted = format(bmc);
    List<String> wrapperMessages = wrapperMessages(chain, bmcIndex);
    // A wrapper holding the formatted text was built by this class, and the error logged then.
    boolean alreadyLogged = wrapperMessages.stream().anyMatch(m -> m.contains(formatted));
    if (logFullError && !alreadyLogged) {
      log.error(
          "{} (opc-request-id: {})",
          context == null ? "OCI request failed" : context,
          bmc.getOpcRequestId(),
          t);
    }
    String message = formatted;
    String innerMessage = null;
    for (String wrapperMessage : wrapperMessages) {
      message = combine(wrapperMessage, innerMessage, message, bmc);
      innerMessage = wrapperMessage;
    }
    return message;
  }

  /**
   * Combines a wrapper's message with the shortened message of the exception it wraps, whose own
   * message is {@code innerMessage}, or null for the SDK exception.
   */
  private static String combine(
      String wrapperMessage, String innerMessage, String shortened, BmcException bmc) {
    if (wrapperMessage.contains(shortened)) {
      return wrapperMessage;
    }
    String firstLine = innerMessage;
    String lastLine = "";
    if (innerMessage == null) {
      // BmcException#getMessage() puts a new timestamp on its second line on every call, so a copy
      // embedded by a wrapper is found by its first and last lines. If the SDK changes that
      // layout, nothing matches and the wrapper's message is shown in full.
      String sdkMessage = StringUtils.stripEnd(bmc.getMessage(), null);
      firstLine = StringUtils.substringBefore(sdkMessage, "\n");
      lastLine = StringUtils.substringAfterLast(sdkMessage, "\n");
    }
    int start = StringUtils.isEmpty(firstLine) ? -1 : wrapperMessage.indexOf(firstLine);
    int end = -1;
    if (start >= 0 && lastLine.isEmpty()) {
      end = start + firstLine.length();
    } else if (start >= 0) {
      int lastLineAt = wrapperMessage.indexOf(lastLine, start + firstLine.length());
      end = lastLineAt < 0 ? -1 : lastLineAt + lastLine.length();
    }
    if (end < 0) {
      return withContext(wrapperMessage, shortened);
    }
    String prefix =
        StringUtils.removeEnd(
            wrapperMessage.substring(0, start), BmcException.class.getName() + ": ");
    return prefix + shortened + wrapperMessage.substring(end);
  }

  static String format(BmcException e) {
    ServiceDetails details = e.getServiceDetails();
    String target =
        details == null
            ? ""
            : Stream.of(details.getServiceName(), details.getOperationName())
                .filter(OCIErrorFormatter::isKnown)
                .collect(Collectors.joining(" "));
    StringBuilder sb =
        new StringBuilder("OCI ").append(target.isEmpty() ? "request" : target).append(" failed: ");

    if (e.isClientSide()) {
      String message = StringUtils.defaultIfBlank(e.getUnmodifiedMessage(), "no response from OCI");
      sb.append(message);
      // No response arrived, so the reason (DNS, TLS, timeout) is in the cause, which the SDK
      // sometimes already embeds in its message.
      List<Throwable> chain = ExceptionUtils.getThrowableList(e);
      Throwable root = chain.get(chain.size() - 1);
      if (root != e && !message.contains(root.toString())) {
        sb.append(". Cause: ").append(root.getClass().getSimpleName());
        if (StringUtils.isNotBlank(root.getMessage())) {
          sb.append(": ").append(root.getMessage());
        }
      }
      return sb.toString();
    }

    // Some services return "detail" instead of, or in addition to, "message".
    Map<String, String> extra = e.getAdditionalProperties();
    String detail = extra == null ? null : extra.get("detail");
    String text =
        StringUtils.firstNonBlank(e.getUnmodifiedMessage(), e.getOriginalMessage(), detail);
    if (text == null) {
      sb.append("no error message in the response");
    } else {
      sb.append(text);
      if (StringUtils.isNotBlank(detail) && !text.contains(detail)) {
        sb.append(text.endsWith(".") ? " " : ". ").append("Detail: ").append(detail);
      }
    }

    int status = e.getStatusCode();
    sb.append(" (").append(status);
    if (StringUtils.isNotBlank(e.getServiceCode())) {
      sb.append(' ').append(e.getServiceCode());
    }
    // OCI answers both "does not exist" and "not authorized" with 404; the region in the endpoint
    // host is often the explanation.
    String host =
        status == 404 && details != null ? endpointHost(details.getRequestEndpoint()) : null;
    if (host != null) {
      sb.append(", endpoint: ").append(host);
    }
    sb.append(')');
    if ((status < 400 || status >= 500) && StringUtils.isNotBlank(e.getOpcRequestId())) {
      sb.append(". OCI request ID: ").append(e.getOpcRequestId());
    }
    return sb.toString();
  }

  private static int indexOfBmcException(List<Throwable> chain) {
    for (int i = 0; i < chain.size(); i++) {
      if (chain.get(i) instanceof BmcException) {
        return i;
      }
    }
    return -1;
  }

  // Innermost first. Skips wrappers that only repeat their cause, such as
  // new RuntimeException(cause).
  private static List<String> wrapperMessages(List<Throwable> chain, int bmcIndex) {
    List<String> messages = new ArrayList<>();
    for (int i = bmcIndex - 1; i >= 0; i--) {
      String message = chain.get(i).getMessage();
      if (StringUtils.isNotEmpty(message) && !message.equals(chain.get(i + 1).toString())) {
        messages.add(message);
      }
    }
    return messages;
  }

  private static String withContext(String context, String message) {
    return context + (context.endsWith(".") ? " " : ": ") + message;
  }

  private static boolean isKnown(String name) {
    return StringUtils.isNotBlank(name) && !name.startsWith(UNKNOWN_PREFIX);
  }

  // Endpoints look like "https://host/path?query", sometimes prefixed with the HTTP method.
  static String endpointHost(String endpoint) {
    String afterScheme = StringUtils.substringAfter(endpoint, "://");
    if (StringUtils.isEmpty(afterScheme)) {
      return null;
    }
    int end = StringUtils.indexOfAny(afterScheme, '/', '?', '#', ' ');
    return end < 0 ? afterScheme : afterScheme.substring(0, end);
  }
}
