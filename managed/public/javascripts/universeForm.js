// Copyright (c) YugaByte, Inc.

$(document).ready(function() {
  $(".alert").hide();

  $.get( "/api/providers", function( results ) {
    var options = $("#provider");
    var selectedProviderUUID = $("#selectedProviderUUID").val();

    $.each(results, function(idx, provider) {
      var optionEle = $("<option />").val(provider.uuid).text(provider.name);
      if (provider.uuid == selectedProviderUUID) {
        optionEle.attr('selected', true);
      }
      options.append(optionEle);
    });

    if (selectedProviderUUID) {
      populateRegions();
    }
  });

  $("input[name=isMultiAZ]", "#instanceForm").on("change", function() {
    populateRegions();
  });

  $("#provider").on('change', function() {
    populateRegions();
  });

  $('#regionList').select2();
});

function populateRegions() {
  var providerID = $("#provider").val();
  var multiAZ = $('input[name=isMultiAZ]:checked', '#instanceForm').val();
  var selectedRegionList = $("#selectedRegionList").val();

  $.get( "/api/providers/" + providerID + "/regions?multiAZ="+ multiAZ , function( results ) {
     var options = $("#regionList");
     options.empty();
     $.each(results, function(idx, region) {
       options.append($("<option />").val(region.uuid).text(region.name));
     });

     var selectedRegionUUIDs = []
     if (selectedRegionList) {
       selectedRegionUUIDs = selectedRegionList.replace(/[[\]]/g,'').split(", ");
       options.val(selectedRegionUUIDs).trigger("change");
     }
   });
}
