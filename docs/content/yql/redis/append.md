---
title: APPEND
---

## SYNOPSIS
% <code>APPEND key string_value</code>
This command appends a value to the end of the string that is associated with the given <code>key</code>.
<li>If <code>key</code> already exists, the given <code>string_value</code> is appended to the end of the string value that is associated with the <code>key</code>.</li>
<li>If the <code>key</code> does not exist, it is created and associated with an empty string.</li>
<li>If the <code>key</code> is associated with a non-string value, an error is raised.</li>

## RETURN VALUE
Returns the length of the resulted string after appending.

## EXAMPLES
% <code>GET yugakey</code><br>
"Yuga"<br>
% <code>APPEND yugakey "Byte"</code><br>
8<br>
% <code>GET yugakey</code><br>
"YugaByte"<br>

## SEE ALSO
