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
  if (typeof $ === 'function' && el instanceof $) {
    el = el[0];
  }

  const rect = el.getBoundingClientRect();

  return (
    rect.top >= 0 &&
    rect.left >= 0 &&
    rect.bottom <= (window.innerHeight || document.documentElement.clientHeight) &&
    rect.right <= (window.innerWidth || document.documentElement.clientWidth)
  );
}

function yugabyteResizeHeaderMenu() {
  $(document).on('click', '.mobile-menu,.open #controls-header-menu', () => {
    $('.page-header').toggleClass('open');
    $('.mobile-menu').toggleClass('open');
    $('.mobile-search').removeClass('open');
    $('.page-header').removeClass('open-search');
  });

  $(document).on('click', 'ul#header-menu-list li', () => {
    if ($(window).width() < 768) {
      $('ul.header-submenu', this).toggleClass('open');
    }
  });

  $(document).on('click', '.mobile-search,.open-search #controls-header-menu', () => {
    $('.page-header').toggleClass('open-search');
    $('.page-header,.mobile-menu').removeClass('open');
    $('.mobile-search').toggleClass('open');
  });

  if ($(window).width() < 768) {
    $('.td-navbar .td-navbar-nav-scroll .navbar-nav').appendTo('.page-header');
  }

  $(document).on('click', '.mobile-right-pannel', (event) => {
    $('.right-panel .more-defination').slideToggle();
    event.stopPropagation();
  });

  $(document).on('click', '.mobile-left-pannel', (event) => {
    $('.left-panel .more-defination').slideToggle();
    event.stopPropagation();
  });

  if ($('.page-header ul.navbar-nav').length > 0 && $(window).width() > 767) {
    $('.page-header ul.navbar-nav').prependTo('#main_navbar');
  }
}

/**
 * Active left navigation depending on the tabs.
 *
 * @param {object} element DOM element
 */
function activeLeftNav(element) {
  const currentUrl = location.pathname;
  const splittedUrl = currentUrl.split('/');
  let leftNavLink = '';

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

      if ($(`aside.td-sidebar nav>ul a[href="${leftNavLink}"]`).length > 0) {
        $(`aside.td-sidebar nav>ul a[href="${leftNavLink}"]`).addClass('current');
        $(`aside.td-sidebar nav>ul a[href="${leftNavLink}"]`).parents('li.submenu').addClass('open');
        return false;
      }
    }
  });
}

$(document).ready(() => {
  let searchValue = '';

  ((document) => {
    const $codes = document.querySelectorAll('pre');
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

  // Open left navigation w.r.t old tab
  if ($('.td-content .nav-tabs-yb .active').length > 0 && $('.td-content .nav-tabs-yb .active').attr('href') !== '') {
    activeLeftNav('.td-content .nav-tabs-yb li');
  }

  // Open left navigation w.r.t new style2 tab
  if ($('.td-content .tabs-style-2 .active').length > 0 && $('.td-content .tabs-style-2 .active').attr('href') !== '') {
    activeLeftNav('.td-content .tabs-style-2 li');
  }

  // Open left navigation w.r.t new style1 tab
  if ($('.td-content .tabs-style-1 .active').length > 0 && $('.td-content .tabs-style-1 .active').attr('href') !== '') {
    activeLeftNav('.td-content .tabs-style-1 li');
  }

  // Change the version dropdown text with the selected version text.
  if ($('#navbarDropdown') && $('.dropdown-menu')) {
    const versionDir = location.pathname.split('/')[1];
    if (versionDir !== '') {
      $('.dropdown-menu a').each((index, element) => {
        if ($(element).attr('href').indexOf(`/${versionDir}/`) !== -1) {
          $('#navbarDropdown').text($(element).text());
          return false;
        }
      });
    }
  }

  // Hide Empty Right sidebar.
  const rt = $('.td-sidebar-toc').text();
  if ($.trim(rt) === '') {
    $('.td-sidebar-toc').remove();
    $('main.col-xl-8').removeClass('col-xl-8');
    $('main').addClass('col-xl-10');
  }

  // Open current page menu
  $(`.left-sidebar-wrap nav > ul.list a[href="${window.location.pathname}"]`).addClass('current').parents('.submenu').addClass('open');

  // Theme color switcher
  document.querySelector('.switcher').onclick = function () {
    const htmlAttr = document.querySelector('html');
    const currentMode = htmlAttr.getAttribute('data-theme');
    const expiryDate = 3600 * 1000 * 24 * 365;

    this.classList.toggle('change');
    if (currentMode === 'orange') {
      htmlAttr.setAttribute('data-theme', 'purple');
      document.cookie = `yb_docs_theme_color=purple; expires=${expiryDate}; path=/`;
    } else {
      htmlAttr.setAttribute('data-theme', 'orange');
      document.cookie = `yb_docs_theme_color=orange; expires=${expiryDate}; path=/`;
    }
  };

  // For Section nav.
  $(document).on('click', '.docs-menu', () => {
    $(this).toggleClass('menu-open');
    $('.left-sidebar-wrap').toggleClass('open');
  });

  // For main menu nav.
  yugabyteResizeHeaderMenu();

  // For Section nav.
  $(document).on('click', 'li.submenu.section a, li.submenu.section i', (event) => {
    if (typeof event.target.href === 'undefined' || event.target.href === '') {
      $(event.currentTarget).parent('li.submenu.section').toggleClass('open');
    }
  });

  $(document).on('click', 'li.submenu:not(.section) i', (event) => {
    $(event.currentTarget).parent('li.submenu:not(.section)').toggleClass('open');
  });

  // Right sidebar click move content to inpage link.
  $(document).on('click', '.td-toc #TableOfContents a,.td-content a', (event) => {
    const linkHref = $(event.currentTarget).attr('href');
    if (!linkHref.startsWith('#')) {
      return;
    }

    window.location.hash = linkHref;
    if ($(window).width() > 767) {
      $('html, body').scrollTop(($(linkHref).offset().top) - 70);
    } else {
      $('html, body').scrollTop(($(linkHref).offset().top) - 140);
    }

    return false;
  });

  // Hash link move page to his inpage link.
  if (window.location.hash && $(`.td-content ${window.location.hash} a`)) {
    $(`.td-content ${window.location.hash} a`).click();
  }

  // Expand / collapse left navigation on click.
  $('.side-nav-collapse-toggle-2').on('click', () => {
    $('aside.td-sidebar').toggleClass('close-bar');
    if ($('aside.td-sidebar').hasClass('close-bar')) {
      const rbw = $('aside.td-sidebar').width();
      const mainW = $('.td-main main').width();
      $('.td-main main').css('min-width', mainW + rbw);
    } else {
      $('.td-main main').removeAttr('style');
    }
  });

  // Expand / collapse left navigation on `[`.
  $(document).keypress((event) => {
    const keycode = (event.keyCode ? event.keyCode : event.which);
    if (keycode === 91) {
      $('.side-nav-collapse-toggle-2').click();
    }
  });

  // Show / Hide left navigation on mouse hover when it is collapsed.
  $('body').on('mouseenter', 'aside.td-sidebar.close-bar .left-sidebar-wrap-inner', (event) => {
    $(event.currentTarget).parent('.left-sidebar-wrap').addClass('parent-active');
    $('body').addClass('active-sb');
    event.stopPropagation();
  }).on('mouseleave', 'aside.td-sidebar.close-bar .left-sidebar-wrap-inner', (event) => {
    $(event.currentTarget).parent('.left-sidebar-wrap').removeClass('parent-active');
    $(event.currentTarget).parent('.left-sidebar-wrap').addClass('parent-active-remove');
    setTimeout(() => {
      $('body').removeClass('active-sb');
      $('.left-sidebar-wrap').removeClass('parent-active-remove');
    }, 500);
    event.stopPropagation();
  });

  document.getElementById('search-form').addEventListener('keyup', (event) => {
    searchValue = event.target.value;
  });

  document.getElementById('search-form').addEventListener('keydown', (event) => {
    if (event.keyCode === 13) {
      window.location.href = `/search/?q=${searchValue}`;
    }
  });

  if (!yugabyteIsElementInViewport($(`.left-sidebar-wrap nav > ul.list a.current`))) {
    const sidebarInnerHeight = $('aside.td-sidebar .left-sidebar-wrap-inner').height();
    const currentTop = $('aside.td-sidebar a.current').offset().top;
    $('aside.td-sidebar nav').scrollTop(currentTop - sidebarInnerHeight);
  }
});

$(window).scroll(() => {
  // Right sidebar inpage link active on scroll.
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

$(window).resize(() => {
  yugabyteResizeHeaderMenu();
});
