(function () {
  /**
   * Decode the cookie and return the appropriate cookie value if found
   * Otherwise empty string is returned.
   *
   * return string
   */
  function getCookie(cname) {
    const cookieName = `${cname}=`;
    const decodedCookie = decodeURIComponent(document.cookie);
    const splittedCookie = decodedCookie.split(';');
    const splittedLength = splittedCookie.length;

    let checkCookie = '';
    let fetchCookie = 0;
    let matchedCookie = '';

    while (fetchCookie < splittedLength) {
      checkCookie = splittedCookie[fetchCookie];
      while (checkCookie.charAt(0)) {
        if (checkCookie.charAt(0) === ' ') {
          checkCookie = checkCookie.substring(1);
        } else {
          break;
        }
      }

      if (checkCookie.indexOf(cookieName) === 0) {
        matchedCookie = checkCookie.substring(cookieName.length, checkCookie.length);
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
    if (location.hostname === 'docs.yugabyte.com') {
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
      if (keyValuePairs[property] !== '') {
        if (property === 'utm_medium' || property === 'utm_source' || property === 'utm_campaign' || property === 'utm_term' || property === 'utm_content') {
          setCookie(property, keyValuePairs[property], 1);
          setUTMs = true;
        }
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

  // Fill Form Values.
  if (utmMedium && utmMedium !== '') {
    if (document.querySelector('li.utm_medium input')) {
      document.querySelector('li.utm_medium input').value = utmMedium;
    }

    availableUTMs.utm_medium = utmMedium;
  }

  if (utmSource && utmSource !== '') {
    if (document.querySelector('li.utm_source input')) {
      document.querySelector('li.utm_source input').value = utmSource;
    }

    availableUTMs.utm_source = utmSource;
  }

  if (utmCampaign && utmCampaign !== '') {
    if (document.querySelector('li.utm_campaign input')) {
      document.querySelector('li.utm_campaign input').value = utmCampaign;
    }

    availableUTMs.utm_campaign = utmCampaign;
  }

  if (utmTerm && utmTerm !== '') {
    if (document.querySelector('li.utm_term input')) {
      document.querySelector('li.utm_term input').value = utmTerm;
    }

    availableUTMs.utm_term = utmTerm;
  }

  if (utmContent && utmContent !== '') {
    if (document.querySelector('li.utm_content input')) {
      document.querySelector('li.utm_content input').value = utmContent;
    }

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
        if (!key.startsWith('utm_')) {
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
