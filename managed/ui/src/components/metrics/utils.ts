/**
 * Returns an array of Prometheus URLs with the appropriate FQDN.
 */
export const getPrometheusUrls = (directUrls: string[], useBrowserFqdn: boolean) =>
  directUrls.map((directUrl) => {
    if (!useBrowserFqdn || window.location.hostname === 'localhost') {
      return directUrl;
    }

    // Use FQDN from the browser window instead.
    const url = new URL(directUrl);
    url.hostname = window.location.hostname;
    return url.href;
  });
