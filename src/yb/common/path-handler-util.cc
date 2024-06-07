// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/common/path-handler-util.h"

#include "yb/util/format.h"

namespace yb {

namespace {

const char* const kSortAndFilterTableScript = R"(
<script>
function castIfNumber(elem) {
 return elem.length ?
          (elem.length > 14 || isNaN(Number(elem)) ?
            elem.toLowerCase() :
            Number(elem)) :
          "~";
}

function sortTable(table_id, n) {
  var asc_symb = ' <span style="color: grey">\u25B2</span>';
  var desc_symb = ' <span style="color: grey">\u25BC</span>';
  var i, swapCount = 0;
  var table = document.getElementById(table_id);
  if (table.rows.length < 3) {
    return;
  }
  var switching = true;
  var asc = true;
  if (table.rows[0].getElementsByTagName("TH")[n].innerHTML.includes(asc_symb)) {
    asc = false;
  }

  for(var j = 0; j < table.rows[0].getElementsByTagName("TH").length; j++) {
   table.rows[0].getElementsByTagName("TH")[j].innerHTML =
    table.rows[0].getElementsByTagName("TH")[j].innerHTML.replace(asc_symb, "").replace(desc_symb,
      "");
    if (j == n) {
      sort_symb = asc ? asc_symb : desc_symb;
      table.rows[0].getElementsByTagName("TH")[j].innerHTML =
        table.rows[0].getElementsByTagName("TH")[j].innerHTML.concat(sort_symb);
    }
  }

  while (switching) {
    switching = false;
    // Ignore header row.
    for (i = 1; i < (table.rows.length - 1); i++) {
      var swap = false;
      var x = table.rows[i].getElementsByTagName("TD")[n];
      var y = table.rows[i + 1].getElementsByTagName("TD")[n];
      var cmpX = castIfNumber(x.innerHTML);
      var cmpY = castIfNumber(y.innerHTML);

      if (asc) {
        if (cmpX > cmpY) {
          swap= true;
          break;
        }
      } else {
        if (cmpX < cmpY) {
          swap = true;
          break;
        }
      }
    }

    if (swap) {
      table.rows[i].parentNode.insertBefore(table.rows[i + 1], table.rows[i]);
      switching = true;
    }
  }
}

function filterTableFunction(input_id, table_id) {
  var filter = document.getElementById(input_id).value.toLowerCase();
  var table = document.getElementById(table_id);
  var tr = table.getElementsByTagName("tr");
  for (var i = 0; i < tr.length; i++) {
    if (tr[i].getElementsByTagName("th").length > 0) {
     // Ignore header rows.
      continue;
    }
    var row = tr[i].getElementsByTagName("td");
    var found = false;
    for (const td of row) {
      if (td) {
        var value = td.textContent || td.innerText;
        if (value.toLowerCase().indexOf(filter) > -1) {
          found = true;
          break;
        }
      }
    }

    if(found) {
      tr[i].style.display = "";
    } else {
      tr[i].style.display = "none";
    }
  }
}
</script>
)";

}  // namespace

std::string GenerateTableFilterBox(const std::string& input_id, const std::string& table_id) {
  return Format(
      "<input type='text' id='$0' onkeyup='filterTableFunction(\"$0\", \"$1\")' "
      "placeholder='Search for ...' title='Type in a text'>\n",
      input_id, table_id);
}

std::string GetSortAndFilterTableScript() { return kSortAndFilterTableScript; }

}  // namespace yb
