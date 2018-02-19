// Copyright (c) YugaByte Inc.

import Clipboard from 'clipboard';

const $ = window.jQuery;
$(document).ready(() => {
  $('#drawerMenu').navgoco({
    accordion: true,
    caretHtml: '',
    cookie: {
      name: 'navgoco',
      path: '/',
      expires: 0,
    },
    toggleSelector: 'a.node-toggle',
    openClass: 'open',
    save: true,
    slide: {
      duration: 150,
      easing: 'swing',
    },
  }).show();

  ((document, Clipboard) => {
    const $codes = document.querySelectorAll('code.copy');

    const addCopy = element => {
      const contentContainer = element.getElementsByTagName('code')[0];
      const content = contentContainer.textContent;
      let index = 0;
      if (contentContainer.classList.contains('separator-gt')) index = content.indexOf('> ') + 2;
      if (contentContainer.classList.contains('separator-dollar')) index = content.indexOf('$ ') + 2;

      const textarea = document.createElement('textarea');
      textarea.value = content.substr(index < 0 ? 0 : index, content.length).trim();
      element.append(textarea);

      const button = document.createElement('button');
      button.className = 'copy unclicked';
      button.textContent = 'copy';
      button.addEventListener('click', e => {
        const elem = e.target;
        elem.classList.remove('unclicked');
        setTimeout(
          () => {
            elem.classList.add('unclicked');
          }, 1500);
      });
      element.append(button);
    };

    for (let i = 0, len = $codes.length; i < len; i++) {
      addCopy($codes[i].parentNode);
    }
    const clipboard = new Clipboard('button.copy', {target: trigger => (trigger.previousElementSibling)});
    clipboard.on('error', e => (e.preventDefault()));
  })(document, Clipboard);
});

$(() => {
  const hash = window.location.hash;
  if (hash) $('ul.nav-tabs-yb a[href="' + hash + '"]').tab('show');
  $('.nav-tabs-yb a').click(function () {
    $(this).tab('show');
    window.location.hash = this.hash;
  });
});

