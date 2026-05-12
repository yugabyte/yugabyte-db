(() => {
  const bannerClosed = browserCookieUtils.getCookie('closeAISearchBanner');
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
      browserCookieUtils.setCookie('closeAISearchBanner', 1);
    });
  }
})();
