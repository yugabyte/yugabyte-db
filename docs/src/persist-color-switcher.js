/**
 * Convert this script into ES5 and minify it from the online tool. Minification
 * code goes after the switcher Element in the HTML to avoid color change time
 * on page load/refresh.
 */
(function () {
  const ybHtmlAttr = document.querySelector('html');
  const ybThemeColor = ybGetCookie('yb_docs_theme_color');

  /**
   * Decode the cookie and return the appropriate cookie value if found
   * Otherwise empty string is returned.
   *
   * @param {string} cname Cookie name.
   *
   * @returns string
   */
  function ybGetCookie(cname) {
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

  if (ybHtmlAttr && ybThemeColor) {
    const ybCurrentMode = ybHtmlAttr.getAttribute('data-theme');
    const themeSwitcher = document.querySelector('.switcher');
    if (ybCurrentMode !== ybThemeColor) {
      ybHtmlAttr.setAttribute('data-theme', ybThemeColor);

      if (themeSwitcher && ybThemeColor === 'orange') {
        themeSwitcher.classList.toggle('change');
      }
    }
  }
})();
