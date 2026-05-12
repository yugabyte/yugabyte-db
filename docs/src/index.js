import Clipboard from 'clipboard';
import { setCookie } from 'browser-cookie-utils';

const $ = window.jQuery;
let yugabytePageFinderList = [];

/**
 * Show popup when the text limit exceed in Pills.
 */
function popupOnPills() {
  $('ul.nav.yb-pills li').each(function () {
    if (($(this).find('a').width() + 25) >= $(this).width()) {
      $(this).addClass('text-overlap');
      $(this).append(`<span class="tooltip">${$(this).find('a').text().trim()}</span>`);
    }
  });
}

/**
 * Check anchor multilines.
 */
function checkAnchorMultilines() {
  $('.td-sidebar nav:not(.fixed-nav) a').each((index, event) => {
    if ($(event).outerHeight() >= 42 && $(event).outerHeight() < 60) {
      $(event).attr('data-lines', 2);
    } else if ($(event).outerHeight() >= 60 && $(event).outerHeight() <= 72) {
      $(event).attr('data-lines', 3);
    } else if ($(event).outerHeight() > 72) {
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
 * Move Right Nav to dropdown in mobile.
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
 * Scroll left navigation depending on the tabs/pills.
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

/**
 * Function to calculate and rotate "Page Finder" to horizontal or vertical design
 * based on the container width.
 */
function yugabytePageFinderWidth() {
  yugabytePageFinderList.forEach(({ width, parent }) => {
    if (parent) {
      const innerContainer = document.querySelector('.content-area');
      if (width > innerContainer.offsetWidth) {
        parent.classList.add('vertical');
        parent.classList.remove('horizontal');
      } else {
        parent.classList.add('horizontal');
        parent.classList.remove('vertical');
      }
    }
  });
}

/**
 * Active left navigation depending on the tabs/pills.
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
        yugabyteScrollLeftNav(document.querySelector(`aside.td-sidebar nav > ul a[href="${leftNavLink}"]`));
        return false;
      }
    }

    return true;
  });
}

/**
 * Add class to `right-nav-auto-scroll` in right menu.
 */
function rightnavAutoScroll() {
  if ($('.td-sidebar-toc .td-toc').innerHeight() + 260 >= window.innerHeight) {
    $('.td-sidebar-toc .td-toc').addClass('right-nav-auto-scroll');
  } else {
    $('.td-sidebar-toc .td-toc').removeClass('right-nav-auto-scroll');
  }
}

$(document).ready(() => {
  const isSafari = /Safari/.test(navigator.userAgent) && /Apple Computer/.test(navigator.vendor);
  if (isSafari) {
    $('body').addClass('is-safari');
  }

  const pageFinderContainer = document.querySelectorAll('.page-finder .finder-panel .inner-container');
  if (pageFinderContainer) {
    yugabytePageFinderList = Array.from(pageFinderContainer).map((element) => ({
      width: element.offsetWidth,
      parent: element.parentElement,
    }));
  }

  let searchValue = '';

  /**
   * Main (Header) Nav.
   */
  (() => {
    // Active main Nav.
    yugabyteActiveMainNav();

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

    $(document).on('click', '.start-now-popup.open + .header-submenu', (event) => {
      $(event.currentTarget.parentNode).find('.open').toggleClass('open');
    });

    $(document).on('click', '.products-dropdown .selected', (event) => {
      $(event.currentTarget).toggleClass('open');
    });

    $(document).on('click', '.products-dropdown .dropdown-submenu', (event) => {
      $(event.currentTarget.parentNode).find('.open').toggleClass('open');
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
    if (document.querySelector('body').classList.contains('td-searchpage')) {
      document.querySelector('.top-nav').classList.add('open-search-top');
    }

    $(document).on('click', '.mobile-search', () => {
      $('.top-nav').toggleClass('open-search-top');
      $('.page-header,.mobile-menu').removeClass('open');
      $('body').removeClass('hidden-scroll');
      $('.mobile-search').toggleClass('open');
    });
  })();

  (() => {
    const contributeEditFilePath = document.querySelector('.contribute-edit-file-path');
    if (contributeEditFilePath) {
      const gitURL = contributeEditFilePath.getAttribute('data-git');
      if (gitURL && gitURL.indexOf('ADD_FILE_PATH_HERE') !== -1) {
        const navBar = document.getElementById('nav_bar');
        const filePath = navBar.getAttribute('data-file');
        if (filePath) {
          const newEditUrl = gitURL.replace('ADD_FILE_PATH_HERE', filePath);
          contributeEditFilePath.setAttribute('href', newEditUrl);
        }
      }
    }
  })();

  /**
   * Left sidebar nav.
   */
  (() => {
    // Open current page menu in sidebar.
    if ($('.left-sidebar-wrap nav:not(.fixed-nav) > ul a.current').length === 0) {
      yugabyteActiveLeftNav();
    }

    $('#dragbar:not(.unmoveable)').mousedown(() => {
      $(document).mousemove((e) => {
        let mouseMoveX = e.pageX + 2;
        if (mouseMoveX < 300) {
          mouseMoveX = 300;
        } else if (mouseMoveX >= 500) {
          mouseMoveX = 500;
        }

        $('.td-sidebar').css({
          width: mouseMoveX,
          maxWidth: mouseMoveX,
        });
        $('body').addClass('dragging');
        yugabytePageFinderWidth();
        rightnavAutoScroll();
      });
    });

    $(document).mouseup(() => {
      const navSidebar = document.querySelector('.td-sidebar');

      let mouseMoveX = 0;
      if (navSidebar && navSidebar.style) {
        mouseMoveX = navSidebar.style.width;
        mouseMoveX = mouseMoveX.replace('px', '');
      }

      $(document).unbind('mousemove');
      if ($('body').hasClass('dragging')) {
        setCookie('leftMenuWidth', mouseMoveX, {
          timeToLive: 3,
          unit: 'month'
        });
        setCookie('leftMenuShowHide', '', {
          timeToLive: 3,
          unit: 'month'
        });
      }

      $('body').removeClass('dragging');
      popupOnPills();
      checkAnchorMultilines();
    });

    // For Section nav.
    $(document).on('click', '.docs-menu', (event) => {
      $(event.currentTarget).toggleClass('menu-open');
      $('.left-sidebar-wrap').toggleClass('open');
      if ($('.td-sidebar').hasClass('stick-bar')) {
        document.querySelector('.side-nav-collapse-toggle-2').click();
      }
    });

    $(document).on('click', '.td-sidebar li.submenu a[role="button"], .td-sidebar li.submenu i', (event) => {
      $(event.currentTarget).parent('li').siblings('.open').removeClass('open');
      if ($(event.currentTarget).parent('li.submenu').hasClass('section')) {
        $(event.currentTarget).parent('li.submenu.section').toggleClass('open');
      } else {
        $(event.currentTarget).parent('li').toggleClass('open');
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
   * Copy heading link into clipboard.
   */
  (() => {
    const headingLinks = document.querySelectorAll('.td-heading-self-link');
    if (!headingLinks || !navigator.clipboard) {
      return;
    }

    headingLinks.forEach((link) => {
      link.addEventListener('click', (event) => {
        const url = window.location.origin + window.location.pathname + event.target.getAttribute('href');
        navigator.clipboard.writeText(url).then(() => {
          link.classList.add('copied');

          setTimeout(() => {
            link.classList.remove('copied');
          }, 1500);
        });
      });
    });
  })();

  /**
   * Check immediate heading before H5 on particular pages to apply divider on them.
   * Like `/preview/reference/configuration/yb-tserver/`.
   */
  (() => {
    if (document.body.classList.contains('configuration')) {
      const headings = document.querySelectorAll('.configuration h2, .configuration h3, .configuration h4, .configuration h5');
      let checkH5 = false;

      headings.forEach(heading => {
        const tag = heading.tagName;

        if (tag === 'H2' || tag === 'H3' || tag === 'H4') {
          checkH5 = true;
        } else if (tag === 'H5' && checkH5) {
          heading.classList.add('first-h5');
          checkH5 = false;
        }
      });
    }
  })();

  /**
   * Add Image Popup.
   */
  (() => {
    const imgPopupData = document.createElement('div');
    const imageClick = document.querySelectorAll('.td-content > img:not(.icon), .td-content p > img, .td-content table img');
    imgPopupData.className = 'img-popup-data';

    let popupCounter = 1;
    document.body.appendChild(imgPopupData);

    imageClick.forEach((img) => {
      img.setAttribute('data-popup', popupCounter);
      const imgSrc = img.getAttribute('src');
      let imgAlt = '';
      if (img.hasAttribute('alt')) {
        imgAlt = ` alt="${img.getAttribute('alt')}"`;
      }

      let imgTitle = '';
      if (img.hasAttribute('title')) {
        imgTitle = ` title="${img.getAttribute('title')}"`;
      }

      imgPopupData.insertAdjacentHTML('beforeend', `<div class="image-popup" data-popup="${popupCounter}"><i class="bg-drop"></i><div class="img-scroll"><i></i><img src="${imgSrc}" ${imgAlt} ${imgTitle}></div></div>`);
      popupCounter += 1;

      img.addEventListener('click', (e) => {
        const currentImg = e.target.getAttribute('data-popup');
        document.querySelector(`.image-popup[data-popup="${currentImg}"]`).classList.add('open');
        document.body.classList.add('image-popped-up');
      });
    });

    /**
     * Close popup on clicking cross.
     */
    document.querySelectorAll('.image-popup i').forEach((popupClose) => {
      popupClose.addEventListener('click', () => {
        document.body.classList.remove('image-popped-up');
        popupClose.closest('.image-popup').classList.remove('open');
      });
    });

    /**
     * Close popup on escape key.
     */
    document.onkeydown = function (event) {
      const keycode = (event.keyCode ? event.keyCode : event.which);
      if (keycode === 27) {
        document.body.classList.remove('image-popped-up');
        document.querySelectorAll('.image-popup.open').forEach((popup) => {
          popup.classList.remove('open');
        });
      }
    };
  })();

  rightnavAppend();

  /**
   * Change all page tabs when single tab is changed.
   */
  (() => {
    $('.td-content ul.nav .nav-link').each((index, element) => {
      let tabId = element.id;
      if (tabId) {
        const regex = /(?<name>.*)-[0-9]+-tab/;
        const found = tabId.match(regex);
        if (found && found.groups) {
          tabId = `${found.groups.name}-tab`;
        }

        $(element).addClass(tabId);
      }
    });

    $(document).on('click', '.td-content .nav[role="tablist"] .nav-link', (event) => {
      if (event.target && event.originalEvent && event.originalEvent.isTrusted) {
        let tabId = event.target.getAttribute('id');

        if (tabId) {
          const regex = /(?<name>.*)-[0-9]+-tab/;
          const found = tabId.match(regex);
          if (found && found.groups) {
            tabId = `${found.groups.name}-tab`;
          }

          $('.td-content .nav[role="tablist"]').each((index, element) => {
            if ($(element).next('.tab-content').children(`.tab-pane[aria-labelledby="${tabId}"]`).length > 0) {
              $('li', element).children('.nav-link').removeClass('active');
              $('li', element).children(`.nav-link.${tabId}`).addClass('active');

              $(element).next('.tab-content').children('.tab-pane').removeClass('active show');
              $(element).next('.tab-content').children(`.tab-pane[aria-labelledby="${tabId}"]`).addClass('active show');
            }
          });
        }
      }
    });
  })();

  (() => {
    const header = document.querySelector('.scrolltop-btn');
    const scrollChange = 50;

    header.addEventListener('click', () => {
      window.scrollTo(0, 0);
    });

    window.addEventListener('scroll', () => {
      const scrollpos = window.scrollY;

      if (scrollpos >= scrollChange) {
        header.classList.add('btn-visible');
      } else {
        header.classList.remove('btn-visible');
      }
    });
  })();

  popupOnPills();
  checkAnchorMultilines();

  ((document) => {
    const $codes = document.querySelectorAll('div:not(.nocopy) > pre');
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
          setTimeout(() => {
            elem.classList.add('unclicked');
          }, 1500);
        });

        container.after(button);
        containerChanges(container);
        let text;
        const clip = new Clipboard(button, {
          text(trigger) {
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

  let lastScrollTop = 0;
  $(window).on('scroll', () => {
    let activeLink = '';

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
          activeLink = $(`.td-toc #TableOfContents a[href="#${headingId}"]`);
          $('.td-toc #TableOfContents a').removeClass('active-scroll');
          activeLink.addClass('active-scroll');
        }
      });

      /*
       * Autoscroll right nav where the right nav is a very long one.
       */
      const tocContainer = $('.td-sidebar-toc .td-toc.right-nav-auto-scroll');
      if (tocContainer.length > 0) {
        const linkOffset = activeLink.length ? activeLink.position().top : 0;
        const containerHeight = tocContainer.height();
        const linkHeight = activeLink.length ? activeLink.outerHeight() : 0;

        let scrollFlag = 'up';
        let currentScroll = window.pageYOffset || document.documentElement.scrollTop;
        if (currentScroll > lastScrollTop) {
          scrollFlag = 'down';
        }
        lastScrollTop = currentScroll <= 0 ? 0 : currentScroll;

        if (scrollFlag === 'down') {
          tocContainer.scrollTop(tocContainer.scrollTop() + linkOffset - (containerHeight - linkHeight) - 20);
        } else if (scrollFlag === 'up') {
          let currentPosition = linkOffset - 145 - linkHeight;
          if (currentPosition > containerHeight) {
            tocContainer.scrollTop(tocContainer.scrollTop() + linkOffset - ((containerHeight / 2) + linkHeight / 2));
          } else if (currentPosition <= 18) {
            tocContainer.scrollTop(tocContainer.scrollTop() + linkOffset - (150 + linkHeight));
          }
        }
      }
    }
  });

  /**
   Sort option in table.
   */
  (() => {
    const tables = document.querySelectorAll('.td-content .table-responsive table.sortable');
    tables.forEach(table => {
      const headersEmpty = table.querySelectorAll('th:empty');
      headersEmpty.forEach((innerDiv) => {
        innerDiv.classList.add('empty-th');
      });

      const headers = table.querySelectorAll('th');
      const tbody = table.tBodies[0];
      const totalRows = tbody.querySelectorAll('tr');

      let sortOrder = 1;
      if (totalRows.length > 1) {
        headers.forEach((header, index) => {
          const sortSpan = document.createElement('span');
          const tdsEmpty = table.querySelectorAll(`td:nth-child(${index + 1})`);

          let emptyCellsCount = 0;
          tdsEmpty.forEach((emptyCell) => {
            if (emptyCell.textContent.trim() !== '') {
              emptyCellsCount += 1;
            }
          });

          if (emptyCellsCount === 0) {
            table.querySelector(`thead th:nth-child(${index + 1})`).classList.add('empty-th');
          }

          sortSpan.textContent = 'Sort';
          sortSpan.classList.add('sort-btn');
          header.appendChild(sortSpan);
          sortSpan.addEventListener('click', (ev) => {
            ev.target.classList.toggle('sorted');
            sortTableByColumn(table, index, sortOrder);
            sortOrder *= -1;
          });
        });
      }
    });

    function sortTableByColumn(table, columnIndex, sortOrder) {
      const tbody = table.tBodies[0];
      const rows = Array.from(tbody.querySelectorAll('tr'));
      const sortedRows = rows.sort((a, b) => {
        let aText = '';
        let bText = '';
        let returnVal = '';

        if (a.cells[columnIndex] && a.cells[columnIndex].textContent) {
          aText = a.cells[columnIndex].textContent.replace(/[,\-(]/g, '').trim();
        }

        if (b.cells[columnIndex] && b.cells[columnIndex].textContent) {
          bText = b.cells[columnIndex].textContent.replace(/[,\-(]/g, '').trim();
        }

        if (aText === '' && bText === '') {
          returnVal = 0;
        } else if (aText === '') {
          returnVal = 1;
        } else if (bText === '') {
          returnVal = -1;
        } else {
          returnVal = sortOrder * aText.localeCompare(bText, 'en', {
            numeric: true,
            sensitivity: 'base',
          });
        }

        return returnVal;
      });

      while (tbody.firstChild) {
        tbody.removeChild(tbody.firstChild);
      }

      tbody.append(...sortedRows);
    }
  })();

  if ($('.component-box').length > 0) {
    $('.component-box li p a').each(function () {
      $(this).parents('li').addClass('linked-box');
    });
  }

  $('.td-search-input:focus').parents('form').addClass('active-input');

  $('#search-form').keyup((event) => {
    searchValue = event.target.value;
  });

  $('#search-form').keydown((event) => {
    const keycode = (event.keyCode ? event.keyCode : event.which);
    if (keycode === 13) {
      window.location.href = `/search/?q=${searchValue}`;
    }
  });

  yugabytePageFinderWidth();
  document.querySelector('.side-nav-collapse-toggle-2').addEventListener('click', () => {
    setTimeout(() => {
      yugabytePageFinderWidth();
    }, 500);
  });

  rightnavAutoScroll();
});

$(window).resize(() => {
  rightnavAppend();
  rightnavAutoScroll();
  $('.td-main .td-sidebar').attr('style', '');
  $('.td-main #dragbar').attr('style', '');
  $('.td-main').attr('style', '');
  setTimeout(() => {
    setCookie('leftMenuWidth', 300, {
      timeToLive: 3,
      unit: 'month'
    });
  }, 1000);
  yugabytePageFinderWidth();
});
