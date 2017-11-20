// Copyright (c) YugaByte Inc.

const $ = window.jQuery;
$(document).ready(() => {
  $('#drawerMenu').navgoco({
    accordion: true,
    caretHtml: '',
    cookie: {
      name: 'navgoco',
      path: '/',
      expires: false,
    },
    toggleSelector: 'a.node-toggle',
    openClass: 'open',
    save: true,
    slide: {
      duration: 150,
      easing: 'swing',
    },
  }).show();
});
