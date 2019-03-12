---
title: SET
linkTitle: SET 
summary: Set a session or system variable
description: SET
menu:
  latest:
    identifier: api-ysql-commands-set
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/cmd_set
isTocNested: true
showAsideToc: true
---

## Synopsis

`SET` command update a run-time control parameter.

## Syntax

### Diagram 
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="611" height="150" viewbox="0 0 611 150"><path class="connector" d="M0 21h5m43 0h30m73 0h20m-103 24q0 5 5 5h5m57 0h21q5 0 5-5m-98-24q5 0 5 5v32q0 5 5 5h83q5 0 5-5v-32q0-5 5-5m5 0h30m175 0h30m36 0h20m-71 0q5 0 5 5v19q0 5 5 5h5m30 0h11q5 0 5-5v-19q0-5 5-5m5 0h30m53 0h41m-109 0q5 0 5 5v19q0 5 5 5h5m74 0h5q5 0 5-5v-19q0-5 5-5m5 0h20m-420 0q5 0 5 5v48q0 5 5 5h5m48 0h10m55 0h30m76 0h20m-106 24q0 5 5 5h5m57 0h24q5 0 5-5m-101-24q5 0 5 5v48q0 5 5 5h5m74 0h7q5 0 5-5v-48q0-5 5-5m5 0h151q5 0 5-5v-48q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="43" height="24" rx="7"/><text class="text" x="15" y="21">SET</text><rect class="literal" x="78" y="5" width="73" height="24" rx="7"/><text class="text" x="88" y="21">SESSION</text><rect class="literal" x="78" y="34" width="57" height="24" rx="7"/><text class="text" x="88" y="50">LOCAL</text><a xlink:href="../grammar_diagrams#configuration-parameter"><rect class="rule" x="201" y="5" width="175" height="24"/><text class="text" x="211" y="21">configuration_parameter</text></a><rect class="literal" x="406" y="5" width="36" height="24" rx="7"/><text class="text" x="416" y="21">TO</text><rect class="literal" x="406" y="34" width="30" height="24" rx="7"/><text class="text" x="416" y="50">=</text><a xlink:href="../grammar_diagrams#value"><rect class="rule" x="492" y="5" width="53" height="24"/><text class="text" x="502" y="21">value</text></a><rect class="literal" x="492" y="34" width="74" height="24" rx="7"/><text class="text" x="502" y="50">DEFAULT</text><rect class="literal" x="201" y="63" width="48" height="24" rx="7"/><text class="text" x="211" y="79">TIME</text><rect class="literal" x="259" y="63" width="55" height="24" rx="7"/><text class="text" x="269" y="79">ZONE</text><a xlink:href="../grammar_diagrams#timezone"><rect class="rule" x="344" y="63" width="76" height="24"/><text class="text" x="354" y="79">timezone</text></a><rect class="literal" x="344" y="92" width="57" height="24" rx="7"/><text class="text" x="354" y="108">LOCAL</text><rect class="literal" x="344" y="121" width="74" height="24" rx="7"/><text class="text" x="354" y="137">DEFAULT</text></svg>

### Grammar
```
SET [ SESSION | LOCAL ] { configuration_parameter { TO | = } { value | 'value' | DEFAULT } |
                          TIME ZONE { timezone | LOCAL | DEFAULT } }
```

Where
- `SESSION` option specifies that the command affects only the current session.

- `LOCAL` option specifies that the command affect only the current transaction. After COMMIT or ROLLBACK, the session-level setting takes effect again.

- `configuration_parameter` specifies the name of a mutable run-time parameter.

- `value` specifies new value of parameter.

## Semantics

- Although the values of a parameter can be set, showed, and reset, the effect of these parameters are not yet supported in YugaByte. The factory-settings or default behaviors will be used for the moment.

## See Also
[`SHOW`](../cmd_show)
[`RESET`](../cmd_reset)
[Other YSQL Statements](..)
