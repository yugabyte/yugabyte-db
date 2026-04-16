(() => {
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

  const bannerClosed = getCookie('closeAISearchBanner');
  const crossButton = document.querySelector('.search-banner .cross-btn');

  if (bannerClosed !== '1') {
    document.querySelector('.search-banner').classList.remove('hidden');
  }

  /**
   * Close search banner.
   */
  if (crossButton) {
    crossButton.addEventListener('click', (event) => {
      event.currentTarget.parentNode.classList.add('hidden');
      document.cookie = 'closeAISearchBanner=1; path=/; secure=true';
    });
  }
})();
