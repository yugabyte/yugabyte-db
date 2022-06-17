import Clipboard from 'clipboard';

const $ = window.jQuery;
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
        button.textContent = 'copy';
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

  // Open left navigation w.r.t tab
  if ($('.td-content .nav-tabs-yb .active').length > 0 && $('.td-content .nav-tabs-yb .active').attr('href') !== '') {
    let page_url = location.pathname;
    page_url = page_url.split('/');
    page_url = page_url.filter(Boolean);
    page_url = page_url.slice(0, -1);
    page_url = `/${page_url.join('/')}/`;

    let tab_link = $('.td-content .nav-tabs-yb li:nth-child(1) a').attr('href');
    let domain = tab_link.split('/');
    domain = `${String(page_url) + domain[domain.length - 2]}/`;

    if ($(`aside.td-sidebar nav>ul a[href="${domain}"]`).length < 1) {
      tab_link = $('.td-content .nav-tabs-yb li:nth-child(2) a').attr('href');
      domain = tab_link.split('/');
      domain = `${String(page_url) + domain[domain.length - 2]}/`;
    }

    $(`aside.td-sidebar nav>ul a[href="${domain}"]`).addClass('current');
    $(`aside.td-sidebar nav>ul a[href="${domain}"]`).parents('li.submenu').addClass('open');
  }

  // Change the version dropdown text with the selected version text.
  if ($('#navbarDropdown') && $('.dropdown-menu')) {
    const versionDir = `/${location.pathname.split('/')[1]}/`;
    $('.dropdown-menu a').each((index, element) => {
      if ($(element).attr('href').indexOf(versionDir) !== -1) {
        $('#navbarDropdown').text($(element).text());
      }
    });
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
    const html_attr = document.querySelector('html');
    const current_mode = html_attr.getAttribute('data-theme');
    this.classList.toggle('change');
    if (current_mode === 'purple') {
      html_attr.setAttribute('data-theme', 'orange');
    } else {
      html_attr.setAttribute('data-theme', 'purple');
    }
  };

  // For Section nav.
  $(document).on('click', '.docs-menu', () => {
    $(this).toggleClass('menu-open');
    $('.left-sidebar-wrap').toggleClass('open');
  });

  // For main menu nav.
  header_menu_resize();

  // For Section nav.
  $(document).on('click', 'li.submenu.section i', (event) => {
    $(event.currentTarget).parent('li.submenu.section').toggleClass('open');
  });

  $(document).on('click', 'li.submenu:not(.section) i', (event) => {
    $(event.currentTarget).parent('li.submenu:not(.section)').toggleClass('open');
  });

  // Right sidebar click move content to inpage link.
  $(document).on('click', '.td-toc #TableOfContents a,.td-content h2 a,.td-content h3 a,.td-content h4 a', (event) => {
    const link_href = $(event.currentTarget).attr('href');
    $('html, body').scrollTop(($(link_href).offset().top) - 80);

    return false;
  });

  // Hash link move page to his inpage link.
  if (window.location.hash) {
    $(`.td-toc #TableOfContents a[href="${window.location.hash}"]`).click();
  }

  // Expand / collapse left navigation on click.
  $('.side-nav-collapse-toggle-2').on('click', () => {
    $('aside.td-sidebar').toggleClass('close-bar');
    if ($('aside.td-sidebar').hasClass('close-bar')) {
      const rbw = $('aside.td-sidebar').width();
      const main_w = $('.td-main main').width();
      $('.td-main main').css('min-width', main_w + rbw);
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
      window.location.href = '/search/?q=' + searchValue;
    }
  });
});

$(window).scroll(() => {
  // Right sidebar inpage link active on scroll.
  if ($('.td-toc #TableOfContents').length > 0) {
    $('.td-content > h2,.td-content > h3').each((index, element) => {
      const offset_top = $(element).offset().top;
      const scroll_top = $(window).scrollTop();
      const id_h = $(element).attr('id');
      if (offset_top - 75 <= scroll_top) {
        $('.td-toc #TableOfContents a').removeClass('active-scroll');
        $(`.td-toc #TableOfContents a[href="#${id_h}"]`).addClass('active-scroll');
      }
    });
  }
});

function header_menu_resize() {
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

$(window).resize(() => {
  header_menu_resize();
});
