/**
 * Compile the code in ES5 and minify it and then add the minify code at the
 * end in the sidebar-tree.html file under `/layouts/partials/`.
 */
(() => {
  'use strict';

  /**
   * Scroll left navigation.
   */
  function yugabyteScrollLeftNav(activeLink) {
    const leftSidebar = document.querySelector('aside.td-sidebar nav:not(.fixed-nav)');

    if (!leftSidebar || !activeLink) {
      return;
    }

    leftSidebar.style.overflow = 'hidden';
    const sidebarInnerHeight = leftSidebar.clientHeight;
    const currentTop = activeLink.getBoundingClientRect().top;
    leftSidebar.scrollTop = currentTop - sidebarInnerHeight;
    setTimeout(() => {
      leftSidebar.style.overflow = 'auto';
    }, 600);
  }

  const currentLink = document.querySelector('.left-sidebar-wrap nav:not(.fixed-nav) > ul a.current');
  if (currentLink) {
    yugabyteScrollLeftNav(currentLink);
  }
})();