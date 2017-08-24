---
title: EXISTS
---

## SYNOPSIS
<code>EXISTS key [key ...]</code><br>
This command is a predicate to check whether or not the given <code>key</code> exists.

<li><code>key</code>.</li>

## RETURN VALUE
Returns the number of existing keys.

## EXAMPLES
% <code>SET yuga1 "Africa"</code><br>
"OK"<br>
% <code>SET yuga2 "America"</code><br>
"OK"<br>
% <code>EXISTS yuga1</code><br>
1<br>
% <code>EXISTS yuga1 yuga2 not_a_key</code><br>
2<br>

## SEE ALSO
