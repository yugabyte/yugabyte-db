package org.yb.ybcli.commands;

import org.springframework.core.Ordered;
import org.springframework.core.annotation.Order;
import org.springframework.shell.plugin.support.DefaultBannerProvider;
import org.springframework.shell.support.util.OsUtils;
import org.springframework.stereotype.Component;

@Component
@Order(Ordered.HIGHEST_PRECEDENCE)
public class YBBannerProvider extends DefaultBannerProvider  {

	@Override
  public String getBanner() {
		StringBuffer buf = new StringBuffer();
		buf.append("=======================================" + OsUtils.LINE_SEPARATOR);
		buf.append("*                                     *" + OsUtils.LINE_SEPARATOR);
		buf.append("*           YugaByte CLI              *" + OsUtils.LINE_SEPARATOR);
		buf.append("*                                     *" + OsUtils.LINE_SEPARATOR);
		buf.append("=======================================" + OsUtils.LINE_SEPARATOR);
		buf.append("Version:" + this.getVersion());
		return buf.toString();
	}

	@Override
  public String getVersion() {
		return "0.1";
	}

	@Override
  public String getWelcomeMessage() {
		return "Welcome to YugaByte CLI";
	}

	@Override
	public String getProviderName() {
		return "YB CLI Banner";
	}
}
