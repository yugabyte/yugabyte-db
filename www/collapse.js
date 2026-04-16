$(function() {
    $('#memtrackerstable').on('click', '.toggle', function() {
      //Gets all <tr>'s  of greater depth
      //below element in the table
      var findChildren = function(tr) {
        var depth = tr.data('depth');
        return tr.nextUntil($('tr').filter(function() {
          return $(this).data('depth') <= depth;
        }));
      };

      var el = $(this);
      var tr = el.closest('tr'); //Get <tr> parent of toggle button
      var children = findChildren(tr);

      //Remove already collapsed nodes from children so that we don't
      //make them visible.
      var subnodes = children.filter('.expand');
      subnodes.each(function() {
        var subnode = $(this);
        var subnodeChildren = findChildren(subnode);
        children = children.not(subnodeChildren);
      });

      //Change icon and hide/show children
      if (tr.hasClass('collapse')) {
        tr.removeClass('collapse').addClass('expand');
        children.hide();
        children.css("display","none");
      } else {
        tr.removeClass('expand').addClass('collapse');
        children.show();
        children.css("display","table-row");
      }
      return children;
    });
  });

$(function() {
  $(".level0 td:first-child").has(".toggle").css("padding-left", "6px");
  $(".level1 td:first-child").has(".toggle").css("padding-left", "21px");
  $(".level2 td:first-child").has(".toggle").css("padding-left", "36px");
  $(".level3 td:first-child").has(".toggle").css("padding-left", "51px");
  $(".level4 td:first-child").has(".toggle").css("padding-left", "66px");
  $(".level5 td:first-child").has(".toggle").css("padding-left", "81px");
  $(".level6 td:first-child").has(".toggle").css("padding-left", "96px");
});
