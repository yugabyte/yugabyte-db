package com.yugabyte.troubleshoot.ts.filters;

import com.yugabyte.troubleshoot.ts.logs.LogsUtil;
import jakarta.servlet.*;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import java.io.IOException;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.MDC;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.core.annotation.Order;
import org.springframework.stereotype.Component;

@Component
@Order(1)
@Slf4j
public class RequestIdFilter implements Filter {

  private final String requestIdHeader;

  @Autowired
  public RequestIdFilter(@Value("${yb.cloud.requestIdHeader}") String requestIdHeader) {
    this.requestIdHeader = requestIdHeader;
  }

  @Override
  public void doFilter(ServletRequest request, ServletResponse response, FilterChain chain)
      throws ServletException, IOException {

    HttpServletRequest req = (HttpServletRequest) request;
    HttpServletResponse res = (HttpServletResponse) response;
    Map<String, String> context = MDC.getCopyOfContextMap();
    String requestId = req.getHeader(this.requestIdHeader);
    if (requestId != null) {
      MDC.put("request-id", requestId);
    } else {
      requestId = UUID.randomUUID().toString();
    }
    MDC.put(LogsUtil.CORRELATION_ID, requestId);
    res.setHeader(this.requestIdHeader, requestId);
    chain.doFilter(request, response);
    if (context == null) {
      MDC.clear();
    } else {
      MDC.setContextMap(context);
    }
  }
}
