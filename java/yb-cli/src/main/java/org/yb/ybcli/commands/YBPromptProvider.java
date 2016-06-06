package org.yb.ybcli.commands;

import org.springframework.core.Ordered;
import org.springframework.core.annotation.Order;
import org.springframework.shell.plugin.support.DefaultPromptProvider;
import org.springframework.stereotype.Component;

@Component
@Order(Ordered.HIGHEST_PRECEDENCE)
public class YBPromptProvider extends DefaultPromptProvider {

	@Override
	public String getPrompt() {
		return "yb> ";
	}

	@Override
	public String getProviderName() {
		return "YugaByte prompt provider";
	}
}
