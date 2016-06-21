// Copyright (c) YugaByte, Inc.

$(document).ready(function() {
  // Hide any error alerts we have
  $(".form-error-alert").hide();
  $('#side-menu').metisMenu();

  document.cookie.split('; ').forEach(function(cookieString) {
    cookie = cookieString.split("=")
      if ((cookie.length == 2) && (cookie[0] == "authToken")) {
        window.authToken = cookie[1];
        // Jquery Ajax Setup so, future ajax requests have the auth token as part of
        // request header.
        $.ajaxSetup({
          beforeSend: function(xhr) {
            xhr.setRequestHeader("X-AUTH-TOKEN", window.authToken);
          }
        });
      }
  });

  fetchInstances();
});

function fetchInstances() {
  var customerUUID = $("#customerUUID").val();
  $.get( "api/customers/" + customerUUID + "/instances" , function( results ) {
    var ulParentNode = $(".instance-list");

    ulParentNode.empty();
    $.each(results, function(idx, instance) {
      ulParentNode.append($("<li><a href='/instances/"+ instance.instanceId + "'>" + instance.name + "</a></li>"));
    });
  });
}

function parseFormErrorResponse(response) {
  var errorsJson = $.parseJSON(response.responseText);
  var errorMessage = "";
  $(".form-group").removeClass("has-error has-feedback");
  $(".help-block").text("");
  $.each(errorsJson.error, function(field, message) {
    $("#"+field).parent().addClass("has-error has-feedback");
    $("small#"+field).text(message);
  });
}

$(document).on("submit", '#loginForm', function() {
  $.ajax( {
    url: "/api/login",
    type: 'POST',
    data: $('#loginForm').serialize(),
    success: function(response) {
      sessionStorage.setItem("authToken", response.authToken);
      window.location.href = "/";
    },
    error: function(response) {
      if(response.status == 401) {
        var errorJson = $.parseJSON(response.responseText);
        $("#loginFormAlert").text(errorJson.error);
        $("#loginFormAlert").show();
      } else {
        $("#loginFormAlert").hide();
        parseFormErrorResponse(response);
      }
    }
  });
  return false;
});

$(document).on("submit", '#editProfileForm', function() {
  $.ajax( {
    url: "/api/customers/" + $("#customerUUID").val(),
    type: 'PUT',
    data: $('#editProfileForm').serialize(),
    success: function(response) {
      sessionStorage.setItem("authToken", response.authToken);
      window.location.href = "/";
    },
    error: function(response) {
      if(response.status == 401) {

      } else {
        parseFormErrorResponse(response);
      }
    }
  });
  return false;
});

$(document).on("submit", '#registerForm', function() {
  $.ajax( {
    url: "/api/register",
    type: 'POST',
    data: $('#registerForm').serialize(),
    success: function(response) {
        sessionStorage.setItem("authToken", response.authToken);
        window.location.href = "/";
    },
    error: function(response) {
        parseFormErrorResponse(response);
    }
  });
return false;
});

$(document).on("click", "#logoutLink", function(event) {
  event.preventDefault();

  $.ajax({
    url: "/api/logout",
    type: "GET",
    beforeSend : function(xhr) {
      if (window.sessionStorage.authToken) {
        xhr.setRequestHeader("X-AUTH-TOKEN", window.sessionStorage.authToken);
      }
    },
    success: function(response) {
      window.location.href = "/";
    },
    error: function(response) {
      // Whatever we have is invalid authToken, we will go ahead and wipe it.
      document.cookie = "authToken=; expires=Thu, 01 Jan 1970 00:00:00 GMT";
      window.location.href = "/";
    }
  });
  return false;
});

$(document).on("submit", '#createInstanceForm', function() {
  var customerUUID = $("#customerUUID").val();

  $.ajax( {
    url: "/api/customers/" + customerUUID + "/instances",
    type: 'POST',
    data: $('#createInstanceForm').serialize(),
    success: function(response) {
        window.location.href = "/";
    },
    error: function(response) {
      parseFormErrorResponse(response);
    }
  });
return false;
});
