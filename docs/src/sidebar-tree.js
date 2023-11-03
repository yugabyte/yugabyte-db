/**
  * Compile the code in ES5 and minify it and then add the minify code at the
  * end in the sidebar-tree.html file under `/layouts/partials/`.
  */
(() => {
  'use strict';

  /**
   * Check anchor multilines.
   */
  function ybCheckAnchorMultilines() {
    jQuery('.td-sidebar nav:not(.fixed-nav) a').each((index, event) => {
      if (jQuery(event).outerHeight() >= 42 && jQuery(event).outerHeight() < 60) {
        jQuery(event).attr('data-lines', 2);
      } else if (jQuery(event).outerHeight() >= 60 && jQuery(event).outerHeight() <= 72) {
        jQuery(event).attr('data-lines', 3);
      } else if (jQuery(event).outerHeight() > 72) {
        jQuery(event).attr('data-lines', 4);
      } else {
        jQuery(event).removeAttr('data-lines');
      }
    });
  }

  /**
   * Decode the cookie and return the appropriate cookie value if found
   * Otherwise empty string is returned.
   *
   * return string
   */
  function ybGetCookie(cname) {
    const cookieName = cname + '=';
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
   * Create Cookie.
   */
  function ybSetCookie(name, value, monthToLive) {
    let cookie = name + '=' + encodeURIComponent(value);
    if (typeof monthToLive !== 'number') {
      monthToLive = 3;
    }
    cookie += '; max-age=' + (monthToLive * 30 * (24 * 60 * 60));
    cookie += '; path=/';
    if (location.hostname !== 'localhost') {
      cookie += '; secure=true';
    }

    document.cookie = cookie;
  }

  /**
   * Get leftMenu toggle Show.
   */
  function ybSideNavVisibility(status) {
    const navSidebar = document.querySelector('aside.td-sidebar');

    let preWidth = 300;
    if (status === 'hide') {
      let leftMenuWidth = navSidebar.style.width;
      if (leftMenuWidth < 300) {
        leftMenuWidth = 300;
      } else if (leftMenuWidth > 500) {
        leftMenuWidth = 300;
      }

      navSidebar.setAttribute('data-pwidth', leftMenuWidth);
      jQuery('.left-sidebar-wrap-inner').animate({
        opacity: '0',
      });
      jQuery('aside.td-sidebar').animate({
        minWidth: '0px',
        width: '60px',
        maxWidth: '60px',
      });
    } else {
      if (ybGetCookie('leftMenuWidth')) {
        preWidth = ybGetCookie('leftMenuWidth');
      }

      if (navSidebar.getAttribute('data-pwidth')) {
        preWidth = navSidebar.getAttribute('data-pwidth');
      }

      if (preWidth < 300) {
        preWidth = 300;
      } else if (preWidth > 500) {
        preWidth = 300;
      }

      jQuery('.left-sidebar-wrap-inner').animate({
        opacity: '1',
      });

      jQuery('aside.td-sidebar').animate({
        width: preWidth,
        maxWidth: preWidth,
      });
    }
  }

  /**
   * Scroll left navigation.
   */
  function ybScrollSidebar(activeLink) {
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
  const leftNavVisible = ybGetCookie('leftMenuShowHide');
  const leftNavWidth = ybGetCookie('leftMenuWidth');
  const navSidebar = document.querySelector('aside.td-sidebar');
  const sidenavCollapse = document.querySelector('.side-nav-collapse-toggle-2');

  if (!navSidebar) {
    return;
  }

  if (leftNavVisible && leftNavVisible !== '') {
    navSidebar.classList.add('toggled-sidebar');
    if (leftNavVisible === 'hide') {
      navSidebar.classList.add('stick-bar');
      ybSideNavVisibility(leftNavVisible);
    }
  }

  if (leftNavWidth && leftNavWidth !== '') {
    jQuery('.td-main').addClass('hide-right-menu');
    if (leftNavVisible === 'hide') {
      navSidebar.setAttribute('data-pwidth', leftNavWidth);
    } else {
      navSidebar.style.width = leftNavWidth + 'px';
      navSidebar.style.maxWidth = leftNavWidth + 'px';
    }

    setTimeout(() => {
      ybCheckAnchorMultilines();
    }, 1000);
  }

  if (currentLink) {
    ybScrollSidebar(currentLink);
  }

  // Expand / collapse left navigation on click.
  sidenavCollapse.addEventListener('click', () => {
    navSidebar.classList.toggle('stick-bar');
    navSidebar.classList.add('toggled-sidebar');
    if (navSidebar.classList.contains('stick-bar')) {
      ybSetCookie('leftMenuShowHide', 'hide', 3);
      ybSideNavVisibility('hide');
    } else {
      ybSetCookie('leftMenuShowHide', 'show', 3);
      ybSideNavVisibility('show');
    }
  });
})();