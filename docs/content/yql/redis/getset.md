---
title: GETSET
---

## SYNOPSIS
<code>GETSET key value</code><br>
This command is an atomic read and write operation that gets the existing value that is associated with the given <code>key</code> while rewriting it with the given <code>value</code>.

<li>If the given <code>key</code> does not exist, the given <code>value</code> is inserted for the <code>key</code>, and null is returned.</li>
<li>If the given <code>key</code> is associated with non-string data, an error is raised.</li>

## RETURN VALUE
Returns the old value of the given <code>key</code>.

## EXAMPLES
% <code>SET yugakey 1</code><br>
"OK"<br>
% <code>GETSET yugakey 2</code><br>
1<br>

## SEE ALSO
