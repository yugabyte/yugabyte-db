---
title: HSTRLEN
---
This command is to seek the length of a string value that is associated with the given entry-key in a hash table that is associated with the given key.

## SYNOPSIS
% <code>HSTRLEN hash_table_key string_entry_key</code>
<li>If the <code>hash_table_key</code> or <code>string_entry_key</code> does not exist, 0 is returned.</li>
<li>If the <code>hash_table_key</code> is associated with a non-hash-table value, an error is raised.</li>
<li>If the <code>string_entry_key</code> is associated with a non-string value, an error is raised.</li>

## RETURN VALUE
Returns the length of the string that is specified by the <code>hash_table_key</code> and the <code>string_entry_key</code>.

## EXAMPLES
% <code>HMSET yugahash L1 America L2 Europe</code><br>
"OK"<br>
% <code>HSTRLEN yugahash L1</code><br>
7<br>

## SEE ALSO
