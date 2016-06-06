package org.yb.ybcli.commands;

import org.springframework.core.Ordered;
import org.springframework.core.annotation.Order;
import org.springframework.shell.plugin.support.DefaultHistoryFileNameProvider;
import org.springframework.stereotype.Component;


@Component
@Order(Ordered.HIGHEST_PRECEDENCE)
public class YBHistoryFileNameProvider extends DefaultHistoryFileNameProvider {

	@Override
  public String getHistoryFileName() {
		return "/tmp/.yb-cli-history.log";
	}

	@Override
	public String getProviderName() {
		return "YB history file name provider";
	}
}
