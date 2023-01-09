import algoliasearch from 'algoliasearch';

(function () {
  'use strict';

  const searchInput = document.getElementById('search-query');
  let searchPage = false;
  if (document.querySelector('body').classList.contains('td-searchpage') !== -1) {
    searchPage = true;
  }

  /**
   * Provided name value from the Query param either from URL or with the passed
   * search query
   */
  function searchURLParameter(name, fetchFrom) {
    if (!fetchFrom) {
      fetchFrom = location.search;
    }

    return decodeURIComponent((new RegExp('[?|&]' + name + '=([^&;]+?)(&|#|;|$)').exec(fetchFrom) || [null, ''])[1].replace(/\+/g, '%20')) || null;
  }

  /**
   * Remove special characcters to avoid XSS attack.
   */
  function htmlEntities(input) {
    return String(input).replace(/[<>'`=\])}[{(]/g, '')
      .replace(/&/g, '&amp;')
      .replace(/'/g, '&#x27;')
      .replace(/\//g, '&#x2F;');
  }

  /**
   * Create a URL that contains query, tab, items etc.
   */
  function createSearchUrl(pager = 1) {
    const searchText = htmlEntities(searchInput.value);

    if (history.pushState) {
      let addQueryParams = '?query=' + searchText.trim();
      if (pager > 1) {
        addQueryParams += '&page=' + pager;
      }

      window.history.pushState({}, '', addQueryParams);
    }
  }

  /**
   * Main Docs section HTML.
   */
  function docsSection(hitIs) {
    let content = '';
    hitIs.forEach(hit => {
      let title = '';
      const hitLevel0 = hit.hierarchy.lvl0;
      const here = hit.url.split('#')[0].split('/').slice(4);
      let breadcrumb = '';
      if (!hitLevel0) {
        return;
      }

      for (let i = 0; i < here.length; i++) {
        if (here[i] !== '') {
          breadcrumb += here[i] + ' > ';
        }
      }
      breadcrumb = breadcrumb.replaceAll('-', ' ');

      // What is .indexOf('i') === 0?????
      if (searchInput.value.indexOf('i') === 0 && searchInput.value.length === 1) {
        title = hitLevel0;
      } else {
        title = hitLevel0.replace(searchInput.value, '<em>' + searchInput.value + '</em>');
      }

      content += '<li>' +
        '<div class="search-title">' +
          '<a href="' + hit.url.replace(/^(?:\/\/|[^/]+)*\//, '/') + '" title="' + title + '">' +
            '<span class="search-title-inner">' + title + '</span>' +
            '<div class="breadcrumb-item">' + breadcrumb.slice(0, -3) + '</div>' +
          '</a>' +
        '</div>' +
      '</li>';
    });

    return content;
  }

  /**
   * Search Pagination HTML.
   */
  function searchPagination(pagerInfo) {
    document.querySelector('#docs').removeAttribute('style');

    const searchpager = document.getElementById(pagerInfo.pagerId);
    const totalResults = pagerInfo.totalHits;
    // For Search page.
    if (document.querySelector('body').classList.contains('td-searchpage')) {
      const currentPage = pagerInfo.currentPage;
      const totalPages = pagerInfo.totalPages;

      let firstPage = '<span class="pager-btn" data-pager="' + 1 + '">1</span>';
      let lastPage = '<span class="pager-btn" data-pager="' + totalPages + '">' + totalPages + '</span>';
      let nextPageHTML = '<span class="right-btn pager-btn" data-pager="' + (currentPage + 1) + '"> &raquo; </span>';
      let pagerClick = '';
      let pagerClickLength = 0;
      let pagerLoop = 0;
      let pagerFull = '';
      let prevPageHTML = '<span class="left-btn pager-btn" data-pager="' + (currentPage - 1) + '"> &laquo; </span>';

      if (currentPage === 1) {
        prevPageHTML = '';
        firstPage = '<span class="pager-btn active-page" data-pager="' + 1 + '">1</span>';
      }

      if (currentPage === totalPages) {
        nextPageHTML = '';
        lastPage = '<span class="pager-btn active-page" data-pager="' + totalPages + '">' + totalPages + '</span>';
      }

      if (totalPages > 1) {
        let pagerStep = 2;
        let loopTill = totalPages - 1;

        pagerFull = firstPage;

        if (currentPage >= 6) {
          pagerStep = currentPage - 3;
        }

        if (currentPage !== totalPages && totalPages > 8) {
          if ((currentPage + 3) < totalPages) {
            loopTill = currentPage + 3;
          } else if ((currentPage + 2) < totalPages) {
            loopTill = currentPage + 2;
          } else if ((currentPage + 1) < totalPages) {
            loopTill = currentPage + 1;
          }
        }

        if (pagerStep > 2) {
          pagerFull += '...';
        }

        for (pagerStep; pagerStep <= loopTill; pagerStep += 1) {
          let activeClass = '';
          if (pagerStep === currentPage) {
            activeClass = ' active-page';
          }

          pagerFull += '<span class="pager-btn' + activeClass + '" data-pager="' + pagerStep + '">' + pagerStep + '</span>';
        }

        if (loopTill < (totalPages - 1)) {
          pagerFull += '...';
        }

        pagerFull += lastPage;
      }

      searchpager.innerHTML = '<nav>' +
        '<div class="pager-area">' +
          '<div class="left-right-button" data-pager-type="' + pagerInfo.pagerType + '">' +
            prevPageHTML +
            '<span class="page-number">' +
              pagerFull +
            '</span>' +
            nextPageHTML +
          '</div>' +
        '</div>' +
      '</nav>' +
      '<span class="left-right-button mobile-button" data-pager-type="' + pagerInfo.pagerType + '"></span>';

      pagerClick = document.querySelectorAll('#' + pagerInfo.pagerId + ' .left-right-button .pager-btn');
      pagerClickLength = pagerClick.length;

      while (pagerLoop < pagerClickLength) {
        pagerClick[pagerLoop].addEventListener('click', searchSettings);
        pagerLoop += 1;
      }
    } else {
      searchpager.innerHTML = '<nav>' +
        '<div class="pager-area">' +
          '<span class="total-result">' + totalResults + ' Results</span>' +
        '</div>' +
        '<a href="/search/?query=' + searchInput.value + '" title="View all results">View all results</a>' +
      '</nav>';
    }
  }

  /**
   * Add queries with filters selected by user and call search algolia function.
   */
  function searchAlgolia() {
    let perPageCount = 8;
    if (searchPage) {
      perPageCount = 10;
    }

    const client = algoliasearch('UMBCUJCBE8', 'a879f0ab89b677264d0c6e087b714fd8');
    const index = client.initIndex('yugabyte_docs');
    const pageItems = searchURLParameter('page');
    const searchOptions = {
      hitsPerPage: perPageCount,
      page: 0,
    };

    if (pageItems && pageItems > 0) {
      searchOptions.page = pageItems - 1;
    }

    index.search(searchInput.value, searchOptions).then(({ hits, nbHits, nbPages, page }) => {
      const searchpagerparent = document.querySelector('#pagination-docs');

      let pagerDetails = {};
      let sectionHTML = '';
      sectionHTML += docsSection(hits);
      if (hits.length > 0) {
        document.getElementById('doc-hit').innerHTML = sectionHTML;
      } else {
        document.getElementById('doc-hit').innerHTML = '<li class="no-result">No Result Found</li>';
      }

      if (nbPages > 1) {
        pagerDetails = {
          currentPage: page + 1,
          pagerId: 'pagination-docs',
          pagerType: 'docs',
          totalHits: nbHits,
          totalPages: nbPages,
        };

        searchPagination(pagerDetails);
        searchpagerparent.className = 'pager results-' + nbHits;
      } else {
        searchpagerparent.className = 'pager results-' + nbHits;

        if (document.getElementById('pagination-docs')) {
          document.getElementById('pagination-docs').innerHTML = '<nav class="pager-area"><span class="total-result">' + nbHits + ' Results</span></nav>';
        }
      }
      if (searchInput.value.length > 0) {
        document.querySelector('.search-result').style.display = 'block';
      } else {
        document.querySelector('.search-result').style.display = 'none';
      }
    });
  }

  /**
   * Runs on keyup or filters click or Pagination click function.
   */
  function searchSettings(e) {
    if (e.type === 'click') {
      if (e.target.classList.contains('pager-btn') && !e.target.classList.contains('disabled')) {
        if (e.target.getAttribute('data-pager')) {
          createSearchUrl(e.target.getAttribute('data-pager'));
          searchAlgolia();
        }
      }
    } else {
      createSearchUrl();
      searchAlgolia();
    }
  }

  /**
   * This function attached all the events which is needed for the search to work
   * properly.
   */
  function addSearchEvents() {
    if (searchURLParameter('query')) {
      searchInput.setAttribute('value', htmlEntities(searchURLParameter('query')));
    }

    document.addEventListener('DOMContentLoaded', () => {
      searchAlgolia();
    });

    // Events binding.
    searchInput.addEventListener('keyup', searchSettings);
    searchInput.addEventListener('paste', searchSettings);
  }

  addSearchEvents();

  document.addEventListener('keydown', (event) => {
    if (event.code === 'Slash' || event.code === 'NumpadDivide') {
      document.querySelector('body').classList.add('search-focus');
      if (searchURLParameter('query')) {
        searchInput.value = searchURLParameter('query');
      } else {
        searchInput.value = '';
      }
      setTimeout(() => {
        searchInput.focus();
        searchInput.select();
      }, 200);
    }
  });

  const ignoreClickOnMeElement = document.querySelector('body:not(.td-searchpage) .search-area');
  if (ignoreClickOnMeElement) {
    document.addEventListener('click', (event) => {
      const isClickInsideElement = ignoreClickOnMeElement.contains(event.target);
      if (!isClickInsideElement) {
        document.querySelector('.search-result').style.display = 'none';
      }
    });
  }
})();
