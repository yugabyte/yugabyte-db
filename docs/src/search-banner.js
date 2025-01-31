(() => {
  const crossButton = document.querySelector('.search-banner .cross-btn');

  let bannerClosed = '';
  if (window.yugabyteGetCookie) {
    bannerClosed = window.yugabyteGetCookie('closeAISearchBanner');
  }

  if (bannerClosed !== '1') {
    document.querySelector('.search-banner').classList.remove('hidden');
  }

  /**
   * Close search banner.
   */
  if (crossButton) {
    crossButton.addEventListener('click', (event) => {
      event.currentTarget.parentNode.classList.add('hidden');
      if (window.yugabyteSetCookie) {
        window.yugabyteSetCookie('closeAISearchBanner', 1, 0);
      }
    });
  }
})();
