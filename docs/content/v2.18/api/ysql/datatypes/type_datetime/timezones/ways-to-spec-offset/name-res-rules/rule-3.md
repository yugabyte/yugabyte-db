---
title: Rule 3 (for string intended to specify the UTC offset) [YSQL]
headerTitle: Rule 3
linkTitle: 3 'set timezone' string not resolved in ~abbrevs.abbrev
description: Substantiates the rule that a string that's intended to identify a UTC offset is never resolved in pg_timezone_abbrevs.abbrev as the argument of 'set timezone' but is resolved there as the argument of 'timezone()' and within a 'text' literal for a 'timestamptz' value. [YSQL]
menu:
  v2.18:
    identifier: rule-3
    parent: name-res-rules
    weight: 30
type: docs
---

{{< tip title="" >}}
A string that's intended to identify a _UTC offset_ is never resolved in _pg_timezone_abbrevs.abbrev_ as the argument of _set timezone_ but is resolved there as the argument of _at time zone_ (and, equivalently, in _timezone()_) and as the argument of _make_timestamptz()_ (and equivalently within a _text_ literal for a _timestamptz_ value).
{{< /tip >}}

You can discover, with _ad hoc_ queries. that the string _AZOT_ occurs uniquely in _pg_timezone_names.abbrev_. Use the function [_occurrences()_](../helper-functions/#function-occurrences-string-in-text) to confirm it thus:

```plpgsql
with c as (select occurrences('AZOT') as r)
select
  (c.r).names_name     ::text as "~names.name",
  (c.r).names_abbrev   ::text as "~names.abbrev",
  (c.r).abbrevs_abbrev ::text as "~abbrevs.abbrev"
from c;
```

This is the result:

```output
 ~names.name | ~names.abbrev | ~abbrevs.abbrev
-------------+---------------+-----------------
 false       | false         | true
```

This means that the string _AZOT_ can be used as a probe, using the function [_legal_scopes_for_syntax_context()_](../helper-functions/#function-legal-scopes-for-syntax-context-string-in-text)_:

```plpgsql
select x from legal_scopes_for_syntax_context('AZOT');
```

This is the result:

```output
 AZOT:               names_name: false / names_abbrev: false / abbrevs_abbrev: true
 ------------------------------------------------------------------------------------------
 set timezone = 'AZOT';                                       > invalid_parameter_value
 select timezone('AZOT', '2021-06-07 12:00:00');              > OK
 select '2021-06-07 12:00:00 AZOT'::timestamptz;              > OK
```

You can copy-and-paste the offending expression and use it as the argument of a _select_ to see the error occurring "live".

**This outcome supports the formulation of the rule that this page addresses.**
