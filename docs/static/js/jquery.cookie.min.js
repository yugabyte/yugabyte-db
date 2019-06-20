/*!
 * jQuery Cookie Plugin v1.3.1
 * https://github.com/carhartl/jquery-cookie
 *
 * Copyright 2013 Klaus Hartl
 * Released under the MIT license
 */
!function(a){"function"==typeof define&&define.amd?define(["jquery"],a):a(jQuery)}(function(a){function b(a){return a}function c(a){return decodeURIComponent(a.replace(e," "))}function d(a){0===a.indexOf('"')&&(a=a.slice(1,-1).replace(/\\"/g,'"').replace(/\\\\/g,"\\"));try{return f.json?JSON.parse(a):a}catch(b){}}var e=/\+/g,f=a.cookie=function(e,g,h){if(void 0!==g){if(h=a.extend({},f.defaults,h),"number"==typeof h.expires){var i=h.expires,j=h.expires=new Date;j.setDate(j.getDate()+i)}return g=f.json?JSON.stringify(g):String(g),document.cookie=[f.raw?e:encodeURIComponent(e),"=",f.raw?g:encodeURIComponent(g),h.expires?"; expires="+h.expires.toUTCString():"",h.path?"; path="+h.path:"",h.domain?"; domain="+h.domain:"",h.secure?"; secure":""].join("")}for(var k=f.raw?b:c,l=document.cookie.split("; "),m=e?void 0:{},n=0,o=l.length;o>n;n++){var p=l[n].split("="),q=k(p.shift()),r=k(p.join("="));if(e&&e===q){m=d(r);break}e||(m[q]=d(r))}return m};f.defaults={},a.removeCookie=function(b,c){return void 0!==a.cookie(b)?(a.cookie(b,"",a.extend({},c,{expires:-1})),!0):!1}});