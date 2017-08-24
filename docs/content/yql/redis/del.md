---
title: DEL
---

## SYNOPSIS
<code>DEL key [key ...]</code><br>
This command is to delete the given <code>key</code>.

<li>If the <code>key</code> does not exist, it is ignored and not counted toward the total number of removed keys.</li>

## RETURN VALUE
Returns number of keys that were removed.

## EXAMPLES
% <code>SET yuga1 "America"</code><br>
"OK"<br>
% <code>SET yuga2 "Africa"</code><br>
"OK"<br>
% <code>DEL yuga1 yuga2 not_a_key</code><br>
2<br>

## SEE ALSO
