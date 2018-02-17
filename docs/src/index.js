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

$(() => {
  const hash = window.location.hash;
  if (hash) $('ul.nav-tabs-yb a[href="' + hash + '"]').tab('show');
  $('.nav-tabs-yb a').click(function () {
    $(this).tab('show');
    window.location.hash = this.hash;
  });
});
