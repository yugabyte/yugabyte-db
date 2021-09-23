// Copyright (c) Yugabyte Inc.

import Clipboard from 'clipboard';
import 'particles.js';

const $ = window.jQuery;
$(document).ready(() => {
  const particlesJS = window.particlesJS;
  if ($('#particles-js').length) {
    particlesJS('particles-js', {
      particles: {
        number: {
          value: 10,
          density: {
            enable: true,
            value_area: 800,
          },
        },
        color: {
          value: '#ffffff',
        },
        shape: {
          type: 'image',
          image: {
            src: '/images/dot.png',
            width: 100,
            height: 100,
          },
        },
        opacity: {
          value: 0.5,
          random: false,
          anim: {
            enable: false,
            speed: 1,
            opacity_min: 0.1,
            sync: false,
          },
        },
        size: {
          value: 11,
          random: true,
          anim: {
            enable: false,
            speed: 8,
            size_min: 4,
            sync: false,
          },
        },
        line_linked: {
          enable: true,
          distance: 450,
          color: '#323A69',
          opacity: 0.2,
          width: 1,
        },
        move: {
          enable: true,
          speed: 2,
          direction: 'none',
          random: false,
          straight: false,
          out_mode: 'out',
          bounce: false,
          attract: {
            enable: false,
            rotateX: 600,
            rotateY: 1200,
          },
        },
      },
      interactivity: {
        detect_on: 'canvas',
        events: {
          onhover: {
            enable: true,
            mode: 'grab',
          },
          onclick: {
            enable: false,
            mode: 'push',
          },
          resize: true,
        },
        modes: {
          grab: {
            distance: 300,
            line_linked: {
              opacity: 0.4,
            },
          },
          bubble: {
            distance: 400,
            size: 40,
            duration: 2,
            opacity: 8,
            speed: 3,
          },
          repulse: {
            distance: 200,
            duration: 0.4,
          },
          push: {
            particles_nb: 4,
          },
          remove: {
            particles_nb: 2,
          },
        },
      },
      retina_detect: true,
    });
  }
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

  $('.expandable-image').click(function () {
    $('#imageModal').modal('show');
    $('#imageModal .modal-body').html($(this).clone());
  });

  $('#imageModal .modal-close-icon').click(() => {
    $('#imageModal').modal('hide');
    $('#imageModal .modal-body').html();
  });

  $('#imageModal').on('hide.bs.modal', () => {
    $('#imageModal .modal-body').html('');
    $('#imageModal').hide();
  });

  ((document, Clipboard) => {
    const $codes = document.querySelectorAll('pre');
    const addCopyButton = element => {
      const container = element.getElementsByTagName('code')[0];
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
            }, 1500);
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
    for (let i = 0, len = $codes.length; i < len; i++) {
      addCopyButton($codes[i]);
    }
  })(document, Clipboard);
});

$(() => {
  const hash = window.location.hash;
  if (hash) {
    $('ul.nav-tabs-yb a[href="' + hash + '"]').tab('show');
  }
  $('.nav-tabs-yb a').click(function () {
    $(this).tab('show');
  });
});

function setupForm(selector, callback) {
  $(selector).submit(event => {
    event.preventDefault();
    const form = $(event.target);
    if (form.valid()) {
      const formJson = form.serializeJSON();
      formJson.email = formJson.email ? formJson.email : 'no-email@yugabyte.com';
      $.ajax({
        type: 'POST',
        crossDomain: true,
        url: 'https://lcruke0kba.execute-api.us-west-2.amazonaws.com/prod/leads',
        data: JSON.stringify(formJson),
        dataType: 'json',
        contentType: 'application/json',
      }).always((formResponse, s) => {
        $(selector).get(0).reset();
        if (!callback) return;
        return callback(event, form, formResponse, s);
      });
    }
  });
}
window.setupForm = setupForm;

function setupRegistrationForm(selector, message = 'Thanks! We\'ll email you soon.') {
  const formValidator = $(selector).validate({
    rules: {
      email: { minlength: 2, required: true },
      first_name: { minlength: 2, required: true },
      last_name: { minlength: 2, required: true },
    },
  });
  formValidator.resetForm();
  setupForm(selector, event => {
    const thankYouNode = $('<div class="submit-thank-you" />').text(message);
    const eventTarget = $(event.target);
    const submitReplaceNode = eventTarget.closest('.submit-replace');
    if (message) {
      (submitReplaceNode.length ? submitReplaceNode : eventTarget).after(thankYouNode);
      setTimeout(() => {
        thankYouNode.fadeOut(500, () => {
          thankYouNode.remove();
        });
      }, 2500);
    }
  });
}
window.setupRegistrationForm = setupRegistrationForm;

/* Commenting this function since it is not used.

function setupContactUsModal(selector) {
  $('#contactUsModal')
    .on('show.bs.modal', event => {
      $('.contact-sales-button').hide();
      const button = $(event.relatedTarget);
      const downloadType = button.data('type');
      const formValidator = $(selector).validate({
        rules: {
          email: { minlength: 2, required: true },
          first_name: { minlength: 2, required: true },
          last_name: { minlength: 2, required: true },
          company: { minlength: 2, required: true },
          phone: { minlength: 10, required: true },
          'action_payload[notes]': { minlength: 2, required: true },

        },
      });
      formValidator.resetForm();
      $('#action_type').val(downloadType);
      sessionStorage.setItem('action_type', downloadType);
    })
    .on('hide.bs.modal', () => {
      $('.contact-sales-button').show();
    });
} */
