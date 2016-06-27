// Copyright (c) YugaByte, Inc.

$(document).ready(function() {
  $(".alert").hide();

  $.get( "api/providers", function( results ) {
    var options = $("#provider");
    $.each(results, function(idx, provider) {
      options.append($("<option />").val(provider.uuid).text(provider.name));
    });
  });

  $("input[name=multiAZ]", "#createInstanceForm").on("change", function() {
    var multiAZ = $(this).val();
    var providerID = $("#provider").val();

    $.get( "api/providers/" + providerID + "/regions?multiAZ="+ multiAZ , function( results ) {
      var options = $("#region");
      options.empty();
      $.each(results, function(idx, region) {
        options.append($("<option />").val(region.uuid).text(region.name));
      });
    });
  });

  $("#provider").on('change', function() {
    var providerID  = $(this).val();
    var multiAZ = $('input[name=multiAZ]:checked', '#createInstanceForm').val();

    $.get( "api/providers/" + providerID + "/regions?multiAZ="+ multiAZ , function( results ) {
      var options = $("#regionUUID");
      options.empty();
      $.each(results, function(idx, region) {
        options.append($("<option />").val(region.uuid).text(region.name));
      });
    });
  });
});

$(document).on("submit", '#createInstanceForm', function() {
  var customerUUID = $("#customerUUID").val();
  $(".alert").hide();

  $.ajax( {
    url: "/api/customers/" + customerUUID + "/instances",
    type: 'POST',
    data: $('#createInstanceForm').serialize(),
    success: function(response) {
      window.location.href = "/";
    },
    error: function(response) {
      if (response.status == 400)
        parseFormErrorResponse(response);
      else
        // TODO: parse the error and show it
        $(".alert").append(errorResponse.responseText).show();
    }
  });
  return false;
});
