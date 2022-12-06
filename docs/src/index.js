import Clipboard from 'clipboard';

const $ = window.jQuery;

/**
 * Whether the element is in view port or not.
 *
 * @param {*} el Element that needs to check.
 *
 * @returns boolean
 */
function yugabyteIsElementInViewport(el) {
  // Special bonus for those using jQuery.
  if (typeof $ === 'function' && el instanceof $ && el.length > 0) {
    el = el[0];
  } else {
    return true;
  }

  const rect = el.getBoundingClientRect();

  return (
    rect.top >= 0 &&
    rect.left >= 0 &&
    rect.bottom <= (window.innerHeight || document.documentElement.clientHeight) &&
    rect.right <= (window.innerWidth || document.documentElement.clientWidth)
  );
}

function checkAnchorMultilines() {
  $('.td-sidebar nav:not(.fixed-nav) a').each((index, event) => {
    if ($(event).outerHeight() >= 42 && $(event).outerHeight() < 60) {
      $(event).attr('data-lines', 2);
    } else if ($(event).outerHeight() >= 60 && $(event).outerHeight() < 72) {
      $(event).attr('data-lines', 3);
    } else if ($(event).outerHeight() >= 72) {
      $(event).attr('data-lines', 4);
    } else {
      $(event).removeAttr('data-lines');
    }
  });
}

/**
 * Active main Nav.
 */
function yugabyteActiveMainNav() {
  const pathName = location.pathname;

  // For exact link match.
  if ($(`#header-menu-list a[href="${pathName}"]`).length > 0) {
    $(`#header-menu-list a[href="${pathName}"]`).parents('li.header-link').addClass('active');
  } else { // When exact link doesn't match.
    const splitPath = pathName.replace(/^\/+|\/+$/g, '').split('/');
    splitPath.pop();

    while (splitPath.length > 0) {
      const checkPath = splitPath.join('/');
      if ($(`#header-menu-list a[href^="/${checkPath}/"]`).length > 0) {
        $(`#header-menu-list a[href^="/${checkPath}/"]`).first().parents('li.header-link').addClass('active');
        break;
      } else {
        splitPath.pop();
      }
    }
  }
}

/**
 * Move Right Nav to dropdown in mobile .
 */
function rightnavAppend() {
  if ($(window).width() < 992) {
    $('.td-navbar .td-navbar-nav-scroll .navbar-nav').appendTo('.page-header');
  }

  if ($('.page-header ul.navbar-nav').length > 0 && $(window).width() > 991) {
    $('.page-header ul.navbar-nav').prependTo('#main_navbar');
  }
}

/**
 * Active left navigation depending on the tabs.
 */
function yugabyteActiveLeftNav() {
  const currentUrl = location.pathname;
  const splittedUrl = currentUrl.split('/');

  let element = '';
  let leftNavLink = '';

  // Open left navigation w.r.t tabs.
  if ($('.td-content .nav-tabs-yb .active').length > 0 && $('.td-content .nav-tabs-yb .active').attr('href') !== '') {
    element = '.td-content .nav-tabs-yb li';
  }

  // Open left navigation w.r.t pills.
  if ($('.td-content .yb-pills .active').length > 0 && $('.td-content .yb-pills .active').attr('href') !== '') {
    element = '.td-content .yb-pills li';
  }

  $(element).each(function () {
    const tabLink = $('a', this).attr('href');

    if (tabLink.indexOf('/') === 0) {
      leftNavLink = tabLink;
    } else if (tabLink.indexOf('../') === 0) {
      const backslashCount = splittedUrl.length - tabLink.match(/\.\.\//g).length - 1;
      const basePath = `${splittedUrl.slice(0, backslashCount).join('/')}/`;

      leftNavLink = basePath + tabLink.replace(/\.\.\//g, '');
    } else if (tabLink.indexOf('./') === 0) {
      leftNavLink = currentUrl + tabLink.replace('./', '/');
    }

    if (leftNavLink !== '') {
      if (leftNavLink.charAt(leftNavLink.length - 1) !== '/') {
        leftNavLink += '/';
      }

      if ($(`aside.td-sidebar nav > ul a[href="${leftNavLink}"]`).length > 0) {
        $(`aside.td-sidebar nav > ul a[href="${leftNavLink}"]`).addClass('current');
        $(`aside.td-sidebar nav > ul a[href="${leftNavLink}"]`).parents('li.submenu').addClass('open');
        return false;
      }
    }
  });
}

/**
 * Left Nav expansion.
 */
function yugabyteDraggabbleSideNav() {
  const rightSidebar = $('.content-parent').width() * 0.7;

  let i = 0;
  let mouseMoveX = 0;
  $('#dragbar:not(.unmoveable)').mousedown(() => {
    $('#mousestatus').html('mousedown' + i++);
    $(document).mousemove((e) => {
      mouseMoveX = e.pageX + 2;
      if (mouseMoveX <= 300) {
        mouseMoveX = 300;
      }

      if ($('.content-parent').width() <= rightSidebar) {
        $('.td-main .td-sidebar-toc').removeClass('d-xl-block');
        $('.td-main').addClass('show-left-menu');
      } else {
        $('.td-main .td-sidebar-toc').addClass('d-xl-block');
        $('.td-main').removeClass('show-left-menu');
      }
      $('.td-sidebar').css({
        width: mouseMoveX,
        maxWidth: mouseMoveX,
      });
    });
  });

  $(document).mouseup(() => {
    $(document).unbind('mousemove');
  });
}

$(document).ready(() => {
  checkAnchorMultilines();

  let searchValue = '';

  /**
   * Main (Header) Nav.
   */
  (() => {
    // Active main Nav.
    yugabyteActiveMainNav();

    // Change the version dropdown text with the selected version text.
    if ($('#navbarDropdown') && $('.dropdown-menu')) {
      const versionDir = location.pathname.split('/')[1];
      if (versionDir !== '') {
        $('.dropdown-menu a').each((index, element) => {
          if ($(element).attr('href').indexOf(`/${versionDir}/`) !== -1) {
            $('#navbarDropdown').html($(element).html());
            return false;
          }
        });
      }
    }

    $(document).on('click', '.header-menu li.dropdown', (event) => {
      if ($(window).width() < 992) {
        if ($(event.currentTarget).hasClass('active')) {
          $('.header-menu li.dropdown.active').removeClass('active');
        } else {
          $('.header-menu li.dropdown.active').removeClass('active');
          $(event.currentTarget).addClass('active');
        }
      }
    });

    $(document).on('click', '#main_navbar .start-now-popup, .right-nav .start-now-popup', (event) => {
      $(event.currentTarget).toggleClass('open');
      if ($(window).width() < 991) {
        $('.page-header').animate({
          scrollTop: $('.header-submenu').offset().top + 350,
        }, 1000);
      }
    });

    $(document).on('click', '.mobile-menu', () => {
      $('.page-header').toggleClass('open');
      $('.mobile-menu').toggleClass('open');
      $('.mobile-search').removeClass('open');
      $('.page-header').removeClass('open-search');
      $('body').toggleClass('hidden-scroll');
    });

    $(document).on('click', 'ul#header-menu-list li', () => {
      if ($(window).width() < 991) {
        $('ul.header-submenu', this).toggleClass('open');
      }
    });

    $(document).on('click', '.mobile-search', () => {
      $('.page-header').toggleClass('open-search');
      $('.page-header,.mobile-menu').removeClass('open');
      $('body').removeClass('hidden-scroll');
      $('.mobile-search').toggleClass('open');
      if ($('.page-header .search-container-wrap').length === 0) {
        $('.page-header').append('<div class="navbar-nav search-container-wrap">' + $('.search-container-wrap').html() + '</div>');
      }
    });
  })();

  /**
   * Left sidebar nav.
   */
  (() => {
    // Open current page menu in sidebar.
    if ($(`.left-sidebar-wrap nav > ul.list a[href="${window.location.pathname}"]`).length > 0) {
      $(`.left-sidebar-wrap nav > ul.list a[href="${window.location.pathname}"]`).addClass('current').parents('.submenu').addClass('open');
    } else {
      yugabyteActiveLeftNav();
    }

    if (!yugabyteIsElementInViewport($('.left-sidebar-wrap nav > ul.list a.current'))) {
      setTimeout(() => {
        const sidebarInnerHeight = $('aside.td-sidebar nav:not(.fixed-nav)').height();
        const currentTop = $('aside.td-sidebar a.current').offset().top;
        $('aside.td-sidebar nav:not(.fixed-nav)').scrollTop(currentTop - sidebarInnerHeight);
      }, 1000);
    }

    // Left Nav draggabble.
    yugabyteDraggabbleSideNav();

    // For Section nav.
    $(document).on('click', '.docs-menu', () => {
      $(this).toggleClass('menu-open');
      $('.left-sidebar-wrap').toggleClass('open');
    });

    $(document).on('click', '.td-sidebar li.submenu a, .td-sidebar li.submenu i', (event) => {
      $(event.currentTarget).parent('li').siblings('.open').removeClass('open');
      if ($(event.currentTarget).parent('li.submenu').hasClass('section')) {
        $(event.currentTarget).parent('li.submenu.section').toggleClass('open');
      } else {
        $(event.currentTarget).parent('li').toggleClass('open');
      }
    });

    // Expand / collapse left navigation on click.
    $('.side-nav-collapse-toggle-2').on('click', () => {
      $('aside.td-sidebar').toggleClass('stick-bar');
      if ($('aside.td-sidebar').hasClass('stick-bar')) {
        $('.left-sidebar-wrap-inner').animate({
          width: '0px',
          opacity: '0',
        });

        $('aside.td-sidebar').animate({
          minWidth: '0px',
          width: '160px',
        });
      } else {
        $('.left-sidebar-wrap-inner').animate({
          width: '100%',
          left: '0',
          opacity: '1',
        });

        $('aside.td-sidebar').animate({
          width: '300px',
        });
      }
    });

    // Expand / collapse left navigation from keyboard using `[` key.
    $(document).keypress((event) => {
      const keycode = (event.keyCode ? event.keyCode : event.which);
      if (keycode === 91) {
        $('.side-nav-collapse-toggle-2').click();
      }
    });
  })();

  /**
   * Right sidebar.
   */
  rightnavAppend();

  ((document) => {
    const $codes = document.querySelectorAll('pre');
    const containerChanges = container => {
      if (container.parentElement) {
        container.parentElement.classList.add('can-be-copied');
        if (container.children && container.children.length > 0) {
          container.parentElement.setAttribute('data-code', container.children.length);
        } else {
          const codeLines = (container.innerText.match(/\r|\n/g) || '').length;
          if (codeLines > 0) {
            container.parentElement.setAttribute('data-code', codeLines);
          } else {
            container.parentElement.setAttribute('data-code', 1);
          }
        }
      }
    };
    const addCopyButton = element => {
      const container = element.getElementsByTagName('code')[0];
      if (!container) {
        return;
      }

      const languageDescriptor = container.dataset.lang;
      let regExpCopy = /a^/;
      if (languageDescriptor) {
        // Then apply copy button
        // Strip the prompt from CQL/SQL languages
        if (['cassandra', 'cql', 'pgsql', 'plpgsql', 'postgres', 'postgresql', 'sql'].includes(languageDescriptor)) {
          if (element.textContent.match(/^[0-9a-z_.:@=^]{1,30}[>|#]\s/gm)) {
            regExpCopy = /^[0-9a-z_.:@=^]{1,30}[>|#]\s/gm;
          }
          // Strip the $ shell prompt
        } else if (['bash', 'sh', 'shell', 'terminal', 'zsh'].includes(languageDescriptor)) {
          if (element.textContent.match(/^\$\s/gm)) {
            regExpCopy = /^\$\s/gm;
          } else if (element.textContent.match(/^[0-9a-z_.:@=^]{1,30}[>|#]\s/gm)) {
            regExpCopy = /^[0-9a-z_.:@=^]{1,30}[>|#]\s/gm;
          }
          // Don't add a copy button to language names that include "output" or "nocopy".
          // For example, `output.xml` or `nocopy.java`.
        } else if (languageDescriptor.includes('output')) {
          return;
        } else if (languageDescriptor.includes('nocopy')) {
          return;
        }
        const button = document.createElement('button');
        button.className = 'copy unclicked';
        button.textContent = '';
        button.addEventListener('click', e => {
          const elem = e.target;
          elem.classList.remove('unclicked');
          setTimeout(
            () => {
              elem.classList.add('unclicked');
            }, 1500,
          );
        });

        container.after(button);
        containerChanges(container);
        let text;
        const clip = new Clipboard(button, {
          text: (trigger) => {
            text = $(trigger).prev('code').text();
            return text.replace(regExpCopy, '');
          },
        });
        clip.on('success error', e => {
          e.clearSelection();
          e.preventDefault();
        });
      }
    };
    for (let i = 0, len = $codes.length; i < len; i += 1) {
      addCopyButton($codes[i]);
    }
  })(document);

  $('.content-parent').on('scroll', () => {
    // Active TOC link on scroll.
    if ($('.td-toc #TableOfContents').length > 0) {
      let rightMenuSelector = '.td-content > h2,.td-content > h3,.td-content > h4';
      if ($('.td-toc').hasClass('hide-h3')) {
        rightMenuSelector = '.td-content > h2';
      } else if ($('.td-toc').hasClass('hide-h4')) {
        rightMenuSelector = '.td-content > h2,.td-content > h3';
      }

      $(rightMenuSelector).each((index, element) => {
        const offsetTop = $(element).offset().top;
        const scrollTop = $(window).scrollTop();
        const headingId = $(element).attr('id');
        if (offsetTop - 75 <= scrollTop) {
          $('.td-toc #TableOfContents a').removeClass('active-scroll');
          $(`.td-toc #TableOfContents a[href="#${headingId}"]`).addClass('active-scroll');
        }
      });
    }
  });

  $('ul.nav.yb-pills li').each(() => {
    const innertext = $(this).find('a').text().trim();
    if (innertext.length >= 29 && ($(this).find('a').find('i').length > 0 || $(this).find('a').find('img').length > 0)) {
      $(this).append(`<span class="tooltip">${innertext}</span>`);
    } else if (innertext.length >= 35 && ($(this).find('a').find('i').length === 0 && $(this).find('a').find('img').length === 0)) {
      $(this).append(`<span class="tooltip">${innertext}</span>`);
    }
  });

  if ($('.component-box').length > 0) {
    $('.component-box li p a').each(function () {
      $(this).parents('li').addClass('linked-box');
    });
  }

  $('#search-form').keyup((event) => {
    searchValue = event.target.value;
  });

  $('#search-form').keydown((event) => {
    const keycode = (event.keyCode ? event.keyCode : event.which);
    if (keycode === 13) {
      window.location.href = `/search/?q=${searchValue}`;
    }
  });
});

$(window).resize(() => {
  rightnavAppend();

  $('.td-main .td-sidebar').attr('style', '');
  $('.td-main #dragbar').attr('style', '');
  $('.td-main').attr('style', '');
  $('body').removeClass('left-menu-scrolling');
  setTimeout(() => {
    yugabyteDraggabbleSideNav();
  }, 1000);
});
