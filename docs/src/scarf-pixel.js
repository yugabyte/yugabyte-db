(function () {
  const PIXEL_SRC = 'https://static.scarf.sh/a.png?x-pxid=cceca3cf-889e-4249-b94b-c4ba1cfaa85c';
  let fired = false;

  /**
   * Load the Scarf analytics pixel, but only once the visitor has granted
   * Performance Cookies (C0002) consent through OneTrust. Mirrors the
   * consent check RudderStack and Kapa AI use for the same category.
   */
  function fireScarfPixel() {
    const activeGroups = window.OnetrustActiveGroups || '';
    if (fired || activeGroups.indexOf('C0002') === -1) {
      return;
    }
    fired = true;

    const img = document.createElement('img');
    img.referrerPolicy = 'no-referrer-when-downgrade';
    img.alt = '';
    img.width = 1;
    img.height = 1;
    img.style.position = 'absolute';
    img.style.width = '1px';
    img.style.height = '1px';
    img.src = PIXEL_SRC;
    document.body.appendChild(img);
  }

  // Fires on every page load and on every consent change.
  window.addEventListener('OneTrustGroupsUpdated', fireScarfPixel);

  window.addEventListener('load', function () {
    if (window.OneTrust && window.OnetrustActiveGroups) {
      fireScarfPixel();
    }
  });
})();
