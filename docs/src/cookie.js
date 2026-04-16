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

if (typeof window !== 'undefined') {
  window.yugabyteGetCookie = yugabyteGetCookie;
}
