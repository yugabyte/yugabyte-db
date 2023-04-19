---
title: Timezones and UTC offsets [YSQL]
headerTitle: Timezones and UTC offsets
linkTitle: Timezones and UTC offsets
description: Explains everything about timezones and UTC offsets. [YSQL]
image: /images/section_icons/api/subsection.png
menu:
  v2.14:
    identifier: timezones
    parent: api-ysql-datatypes-datetime
    weight: 40
type: indexpage
---

{{< tip title="Understanding the purpose of specifying the timezone depends on understanding the 'timestamp and timestamptz' section." >}}
To understand when, and why, you should specify the timezone (or, more carefully stated, the offset with respect to the _UTC Time Standard_) at which an operation will be performed, you need to understand the _timestamptz_ data type, and converting its values to/from plain _timestamp_ values. See the [plain _timestamp_ and _timestamptz_ data types](../date-time-data-types-semantics/type-timestamp/) section.
{{< /tip >}}

{{< note title="The single word spelling, 'timezone' is used throughout the prose of the YSQL documentation." >}}
The two spellings _"timezone"_, as one word, and _"time zone"_, as two words, both occur in SQL syntax. For example both _set timezone = \<arg\>_  and _set time zone \<arg\>_ are legal. On the other hand, you can decorate a plain _timestamp_ or a _timestamptz_ value with the _at time zone_ operator—but here spelling it as the single word _"timezone"_ causes an error. In contrast, the name of the session variable, as is used in the invocation of the built-in function, must be spelled as a single word: _current_setting('timezone')_.

Usually, the spelling of both the single word and the two separate words is case insensitive. Exceptionally, the column in the catalog view _pg_settings.name_ includes the value _'TimeZone'_. Of course, SQL queries against this view must respect this mixed case spelling.

The YSQL documentation, in the prose accounts, always spells _"timezone"_ as a single word. And where the SQL syntax allows a choice, it is always spelled as a single word there too.
{{< /note >}}

It's very likely indeed that anybody who has reason to read the YSQL documentation has large experience of attending, and arranging, meetings where the participants join remotely from locations spread over the planet. These days, this experience is commonplace, too, for anybody whose network of friends and relatives spans several countries, several states in North America, or the like—and who takes part in virtual social gatherings. Such meetings invariably use an online calendar that allows anyone who consults it to see the dates and times in their own [local time](../conceptual-background/#wall-clock-time-and-local-time).

Some of these calendar implementations will use a PostgreSQL or YugabyteDB database. Systems backed by these two databases (or, for that matter, by _any_ SQL database) would doubtless represent events by their starts and ends, using a pair of  [_timestamptz_](../date-time-data-types-semantics/type-timestamp/) values—or, maybe by their start and duration, using a _[timestamptz_, _[interval](../date-time-data-types-semantics/type-interval/)]_ value tuple.

**The _timestamptz_ data type is overwhelmingly to be preferred over plain _timestamp_**.

This is because, using _timestamptz_, the ability to see an [absolute time](../conceptual-background/#absolute-time-and-the-utc-time-standard) value as a local time in any [timezone](../conceptual-background/#timezones-and-the-offset-from-the-utc-time-standard) of interest is brought simply and declaratively by setting the session's timezone environment variable.

In other words, the valuable semantic properties of the _timestamptz_ data type are brought by the session _TimeZone_ notion and are inseparable from it.

In the calendar use case, the setting will, ideally, be to  the _real_, and _canonically named_, timezone to which the reader's location belongs. PostgreSQL and YugabyteDB use an implementation of the _[tz&nbsp;database](https://en.wikipedia.org/wiki/Tz_database)_. And the notions _real_ and _canonically named_ come from that. See the section [The _extended_timezone_names_ view](./extended-timezone-names/).

The _extended_timezone_names_ view joins the _tz&nbsp;database_ data to the _pg_timezone_names_ view. It's critically important to understand how the facts that this catalog view presents relate to those that the _pg_timezone_abbrevs_ catalog view presents.

**This page has these child pages:**

- [The _pg_timezone_names_ and _pg_timezone_abbrevs_ catalog views](./catalog-views/)

- [The _extended_timezone_names_ view](./extended-timezone-names/)

- [Scenarios that are sensitive to the _UTC offset_ or explicitly to the timezone](./timezone-sensitive-operations/)

- [Four ways to specify the _UTC offset_](./ways-to-spec-offset/)

- [Three syntax contexts that use the specification of a _UTC offset_](./syntax-contexts-to-spec-offset/)

- [Recommended practice for specifying the _UTC offset_](./recommendation/)
