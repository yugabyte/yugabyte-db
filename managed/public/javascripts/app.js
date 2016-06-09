// Copyright (c) Yugabyte, Inc.
$(document).ready(function() {
  document.cookie.split('; ').forEach(function(cookieString) {
    cookie = cookieString.split("=")
      if ((cookie.length == 2) && (cookie[0] == "authToken"))
        window.authToken = cookie[1];
    });
});

function parseFormErrorResponse(response) {
    var errorsJson = $.parseJSON(response.responseText);
    var errorMessage = "";
    $(".form-group").removeClass("has-error has-feedback");
    $(".help-block").text("");
    $.each(errorsJson, function(field, message) {
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
        error: function(httpObj, response) {
            if(httpObj.status == 401) {
                
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
            console.log(response);
        }
    });
    return false;
});

