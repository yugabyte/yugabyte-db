(function () {
  /**
   * Decode the cookie and return the appropriate cookie value if found
   * Otherwise empty string is returned.
   *
   * return string
   */
  function getCookie(cname) {
    const splittedCookie = document.cookie.split(';');
    const splittedLength = splittedCookie.length;

    let fetchCookie = 0;
    let matchedCookie = '';

    while (fetchCookie < splittedLength) {
      const cookiePair = splittedCookie[fetchCookie].split('=');
      if (cname === cookiePair[0].trim() && cookiePair[1].trim() !== '') {
        matchedCookie = decodeURIComponent(cookiePair[1]);
        break;
      }

      fetchCookie += 1;
    }

    return matchedCookie;
  }

  /**
   * Get Query Parameters from the URL and set them in object.
   *
   * return object
   */
  function getQueryParams(url) {
    const params = {};
    const queryString = url.split('?')[1];

    let paramsArray;
    if (queryString) {
      paramsArray = queryString.split('&');
      paramsArray.forEach((param) => {
        const splittedParam = param.split('=');
        if (splittedParam[0] && splittedParam[1]) {
          params[splittedParam[0]] = splittedParam[1];
        }
      });
    }

    return params;
  }

  /**
   * Set cookie.
   */
  function setCookie(name, value, monthToLive) {
    let cookie = `${name}=${encodeURIComponent(value)}`;
    let saveFor = monthToLive;

    if (typeof monthToLive !== 'number') {
      saveFor = 3;
    }

    cookie += `; max-age=${(saveFor * 30 * (24 * 60 * 60))}`;
    if (window.location.hostname.indexOf('.yugabyte.com') !== -1) {
      cookie += '; domain=.yugabyte.com';
    }

    cookie += '; path=/';
    cookie += '; secure=true';
    document.cookie = cookie;
  }

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
        setCookie(property, keyValuePairs[property], 1);
        setUTMs = true;
      }
    });

    if (setUTMs) {
      setCookie('utm_check', true, 1);
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
