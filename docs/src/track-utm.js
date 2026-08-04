import { getCookie, setCookie } from 'browser-cookie-utils';
import { getQueryParams } from 'browser-query-utils';

(function () {
  const availableUTMs = {};
  let utmCondition = false;

  if (getCookie('utm_check')) {
    utmCondition = getCookie('utm_check');
  }

  if (utmCondition === false) {
    const keyValuePairs = getQueryParams(window.location.href);
    let setUTMs = false;

    // Create Cookie for UTM parameters.
    Object.keys(keyValuePairs).forEach((property) => {
      if (property.indexOf('utm_') === 0 && keyValuePairs[property] !== '') {
        setCookie(property, keyValuePairs[property], {
          timeToLive: 1,
          unit: 'month'
        });
        setUTMs = true;
      }
    });

    if (setUTMs) {
      setCookie('utm_check', true, {
        timeToLive: 1,
        unit: 'month'
      });
    }
  }

  const utmCampaign = getCookie('utm_campaign');
  const utmMedium = getCookie('utm_medium');
  const utmSource = getCookie('utm_source');
  const utmTerm = getCookie('utm_term');
  const utmContent = getCookie('utm_content');

  if (utmMedium) {
    availableUTMs.utm_medium = utmMedium;
  }

  if (utmSource) {
    availableUTMs.utm_source = utmSource;
  }

  if (utmCampaign) {
    availableUTMs.utm_campaign = utmCampaign;
  }

  if (utmTerm) {
    availableUTMs.utm_term = utmTerm;
  }

  if (utmContent) {
    availableUTMs.utm_content = utmContent;
  }

  if (Object.keys(availableUTMs).length > 0) {
    const allAnchors = document.querySelectorAll('a');
    allAnchors.forEach((singleAnchor) => {
      const currentParams = {};
      let anchorUrl = singleAnchor.getAttribute('href');

      if (!singleAnchor.hostname) {
        return;
      }

      if (singleAnchor.hostname !== 'info.yugabyte.com' && singleAnchor.hostname !== 'cloud.yugabyte.com') {
        return;
      }

      const anchorParams = getQueryParams(anchorUrl);
      Object.keys(anchorParams).forEach((key) => {
        if (key.indexOf('utm_') !== 0) {
          currentParams[key] = anchorParams[key];
        } else if (!availableUTMs[key]) {
          currentParams[key] = anchorParams[key];
        }
      });

      const totalParams = Object.assign(currentParams, availableUTMs);
      if (Object.keys(totalParams).length > 0) {
        if (anchorUrl.includes('?')) {
          anchorUrl = anchorUrl.split('?')[0];
        }

        Object.keys(totalParams).forEach((key, index) => {
          if (index === 0) {
            anchorUrl += `?${key}=${totalParams[key]}`;
          } else {
            anchorUrl += `&${key}=${totalParams[key]}`;
          }
        });

        singleAnchor.setAttribute('href', anchorUrl);
      }
    });
  }
})();
