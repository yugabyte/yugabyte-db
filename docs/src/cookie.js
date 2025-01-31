/**
 * Decode the cookie and return the appropriate cookie value if found
 * Otherwise empty string is returned.
 *
 * return string
 */
function yugabyteGetCookie(cname) {
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
 * Set cookie.
 *
 * @param {string} name Cookie name.
 * @param {string} value Cookie value.
 * @param {string} monthToLive Cookie expiry date.
 */
function yugabyteSetCookie(name, value, monthToLive) {
  let cookie = `${name}=${encodeURIComponent(value)}`;
  let saveFor = monthToLive;

  if (typeof saveFor !== 'number') {
    saveFor = 3;
  }

  if (saveFor > 0) {
    cookie += `; max-age=${(saveFor * 30 * (24 * 60 * 60))}`;
  }

  if (window.location.hostname.indexOf('.yugabyte.com') !== -1) {
    cookie += '; domain=.yugabyte.com';
  }

  cookie += '; path=/';
  if (location.hostname !== 'localhost') {
    cookie += '; secure=true';
  }

  document.cookie = cookie;
}

if (typeof window !== 'undefined') {
  window.yugabyteGetCookie = yugabyteGetCookie;
  window.yugabyteSetCookie = yugabyteSetCookie;
}
