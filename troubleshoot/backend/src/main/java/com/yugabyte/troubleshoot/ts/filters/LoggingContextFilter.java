package com.yugabyte.troubleshoot.ts.filters;

import static com.yugabyte.troubleshoot.ts.logs.LogsUtil.UNIVERSE_ID;

import com.google.common.collect.ImmutableMap;
import jakarta.servlet.*;
import jakarta.servlet.http.HttpServletRequest;
import java.io.IOException;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.MDC;
import org.springframework.core.annotation.Order;
import org.springframework.stereotype.Component;

@Component
@Order(2)
@Slf4j
public class LoggingContextFilter implements Filter {

  private static final Map<String, String> PARAM_TO_MDC_KEY =
      ImmutableMap.of("universe_uuid", UNIVERSE_ID);

  @Override
  public void doFilter(ServletRequest request, ServletResponse response, FilterChain chain)
      throws ServletException, IOException {

    HttpServletRequest req = (HttpServletRequest) request;
    Map<String, String> context = MDC.getCopyOfContextMap();
    for (Map.Entry<String, String> param : PARAM_TO_MDC_KEY.entrySet()) {
      String paramValue = req.getParameter(param.getKey());
      if (paramValue != null) {
        MDC.put(param.getValue(), paramValue);
      }
    }
    chain.doFilter(request, response);
    if (context == null) {
      MDC.clear();
    } else {
      MDC.setContextMap(context);
    }
  }
}
