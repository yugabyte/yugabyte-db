const $ = window.jQuery;

(() => {
  let clearActiveStorage = 0;

  /**
   * Get saved data from localStorage.
   *
   * @param {string} storageName Session storage name.
   *
   * return string
   */
  function yugabyteGetLocalStorage(storageName) {
    let activeTab = '';
    if (typeof Storage !== 'undefined') {
      activeTab = localStorage.getItem(storageName);
    }

    return activeTab;
  }

  /**
   * Remove saved data from localStorage.
   *
   * @param {string} storageName Session storage name.
   */
  function yugabyteRemoveLocalStorage(storageName) {
    if (typeof Storage !== 'undefined') {
      localStorage.removeItem(storageName);
    }
  }

  /**
   * Save data to localStorage.
   *
   * @param {string} storageName  Session storage name.
   * @param {string} storageValue Session storage value.
   */
  function yugabyteSetLocalStorage(storageName, storageValue) {
    if (typeof Storage !== 'undefined') {
      localStorage.setItem(storageName, storageValue);
    }
  }

  /**
   * Reset cookies to avoid redirect loop.
   *
   * @param {number} time Timespan.
   */
  function yugabyteResetkey(time) {
    setTimeout(() => {
      yugabyteSetLocalStorage('yugabyte-active-tab-common', 'true');
      yugabyteSetLocalStorage('yugabyte-active-tab-sql', 'true');
      yugabyteSetLocalStorage('yugabyte-active-tab-driver', 'true');
      yugabyteSetLocalStorage('yugabyte-active-tab-ybdb', 'true');
      yugabyteSetLocalStorage('yugabyte-active-tab-os', 'true');
      yugabyteSetLocalStorage('yugabyte-active-tab-cl', 'true');
    }, time);
  }

  /**
   * Change tab if cookie exists.
   *
   * @param {string} StorageName Local Storage name.
   * @param {string} tabSelector Tab to trigger.
   * @param {number} activeTabStorageName Tab name which needs to be active.
   */
  function triggerTabByStorage(StorageName, tabSelector, activeTabStorageName) {
    const selectedTabTriggered = yugabyteGetLocalStorage(activeTabStorageName);
    if (selectedTabTriggered !== 'true') {
      const selectedTab = yugabyteGetLocalStorage(StorageName);
      if (selectedTab) {
        const lis = document.querySelectorAll(tabSelector + ' li');
        lis.forEach((li) => {
          const containsCookie = li.innerText.includes(selectedTab);
          if (containsCookie) {
            if (window.location.href !== li.querySelector('a').href) {
              if (!li.classList.contains('tab-active') && !li.classList.contains('active')) {
                yugabyteSetLocalStorage(activeTabStorageName, 'true');
                li.classList.add('tab-active');
                setTimeout(() => {
                  li.querySelector('a').click();
                }, 500);
              }
            }
          }
        });
      }
    }
  }

  $(document).on('click', 'ul[data-target="operating-system"] li', (event) => {
    const activeName = $(event.currentTarget).text().trim();

    yugabyteSetLocalStorage('tab-os', activeName);
    yugabyteResetkey(500);
  });

  $(document).on('click', 'ul[data-target="cluster"] li', (event) => {
    const activeName = $(event.currentTarget).text().trim();

    yugabyteSetLocalStorage('tab-cluster', activeName);
    yugabyteResetkey(500);
  });

  $(document).on('click', 'ul[data-target="ybdb"] li', (event) => {
    const activeName = $(event.currentTarget).text().trim();

    yugabyteSetLocalStorage('tab-ybdb', activeName);
    yugabyteResetkey(500);
  });

  $(document).on('click', 'ul[data-target="sql"] li', (event) => {
    const activeName = $(event.currentTarget).text().trim();

    yugabyteSetLocalStorage('tab-sql', activeName);
    yugabyteResetkey(500);
  });

  $(document).on('click', 'ul[data-target="driver"] li', (event) => {
    const activeName = $(event.currentTarget).text().trim();

    yugabyteSetLocalStorage('tab-driver', activeName, 0);
    yugabyteResetkey(500);
  });

  $(document).on('click', 'ul[data-target="common"] li', (event) => {
    const activeName = $(event.currentTarget).text().trim();

    yugabyteSetLocalStorage('tab-driver', activeName);
    yugabyteResetkey(500);
  });

  const triggerTabAfterContent = setInterval(() => {
    const contentLoaded = document.querySelector('.content-area .td-content');
    if (contentLoaded) {
      triggerTabByStorage('tab-os', 'ul[data-target="operating-system"]', 'yugabyte-active-tab-os');
      triggerTabByStorage('tab-cluster', 'ul[data-target="cluster"]', 'yugabyte-active-tab-cl');
      triggerTabByStorage('tab-ybdb', 'ul[data-target="ybdb"]', 'yugabyte-active-tab-ybdb');
      triggerTabByStorage('tab-sql', 'ul[data-target="sql"]', 'yugabyte-active-tab-sql');
      triggerTabByStorage('tab-driver', 'ul[data-target="driver"]', 'yugabyte-active-tab-driver');
      triggerTabByStorage('tab-common', 'ul[data-target="common"]', 'yugabyte-active-tab-common');

      clearInterval(triggerTabAfterContent);
    }
  }, 10);

  const activeTabInterval = setInterval(() => {
    if (clearActiveStorage >= 20) {
      clearInterval(activeTabInterval);
    }

    yugabyteRemoveLocalStorage('yugabyte-active-tab-common');
    yugabyteRemoveLocalStorage('yugabyte-active-tab-sql');
    yugabyteRemoveLocalStorage('yugabyte-active-tab-driver');
    yugabyteRemoveLocalStorage('yugabyte-active-tab-os');
    yugabyteRemoveLocalStorage('yugabyte-active-tab-cl');
    yugabyteRemoveLocalStorage('yugabyte-active-tab-ybdb');

    clearActiveStorage++;
  }, 1500);
})();
