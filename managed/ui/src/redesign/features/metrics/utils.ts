import { RuntimeConfigKey } from '../../helpers/constants';

export const getPrometheusBaseUrl = (prometheusUrl: string, useBrowserFqdn: boolean) => {
  if (!useBrowserFqdn) {
    return prometheusUrl;
  }

  // Use FQDN from the browser window instead.
  const url = new URL(prometheusUrl);
  url.hostname = window.location.hostname;
  return url.href;
};
