---
title: ysqlsh pset options
headerTitle: ysqlsh pset options
linkTitle: pset options
description: YSQL shell pset meta-command options.
headcontent: Options for the \pset meta-command
menu:
  stable:
    identifier: ysqlsh-pset-options
    parent: ysqlsh-meta-commands
    weight: 10
type: docs
---

The [\pset meta-command](../ysqlsh-meta-commands/#pset-option-value) is used to change the display of the output of query result tables. This page describes the available options for changing display properties using `\pset`.

**Syntax**:

```output
\pset [ option [ value ] ]
```

- *option* indicates which option is to be set.
- *value* varies depending on the selected option.

`\pset` without any arguments displays the current status of all printing options.

For examples using `\pset`, see [ysqlsh meta-command examples](../ysqlsh-meta-examples/).

## Options

### border

*value* must be a number. In general, the higher the number, the more borders and lines the tables have, but details depend on the particular format. In HTML, this translates directly into the `border=...` attribute. In most other formats only values `0` (no border), `1` (internal dividing lines), and `2` (table frame) make sense, and values greater than `2` are treated as `border = 2`. The latex and latex-longtable formats additionally allow a value of `3` to add dividing lines between data rows.

### columns

Sets the target width for the `wrapped` format, and also the width limit for determining whether output is wide enough to require the pager or switch to the vertical display in expanded auto mode. The default value `0` causes the target width to be controlled by the environment variable [COLUMNS](../ysqlsh/#columns), or the detected screen width if `COLUMNS` isn't set. In addition, if columns is `0`, then the `wrapped` format only affects screen output. If columns is nonzero, then file and pipe output is wrapped to that width as well.

### expanded (or x)

If *value* is specified it must be either `on` or `off`, which enables or disables expanded mode, or `auto`. If *value* is omitted, the command toggles between the `on` and `off` settings. When expanded mode is enabled, query results are displayed in two columns; the column name is on the left, and the data on the right. This is helpful if the data won't fit on the screen in the normal horizontal mode. In the `auto` setting, the expanded mode is used whenever the query output has more than one column and is wider than the screen; otherwise, the regular mode is used. The `auto` setting is only effective in the aligned and wrapped formats. In other formats, it always behaves as if the expanded mode is `off`.

### fieldsep

Specifies the field separator to use in unaligned output format. You can create, for example, tab- or comma-separated output, which other programs might prefer. To set a tab as field separator, run the command `\pset fieldsep '\t'`. The default field separator is `|` (a vertical bar).

### fieldsep_zero

Sets the field separator to use in unaligned output format to a zero byte.

### footer

If a *value* is specified, it must be either `on` or `off`, which enables or disables display of the table footer (the (n rows) count). If the value is omitted, the command toggles footer display `on` or `off`.

### format

Sets the output format to one of `unaligned`, `aligned`, `wrapped`, `html`, `asciidoc`, `latex` (uses `tabular`), `latex-longtable`, or `troff-ms`. Unique abbreviations are allowed.

`unaligned` format writes all columns of a row on one line, separated by the currently active field separator. Use this option to create output intended to be read in by other programs (for example, tab-separated or comma-separated format).

`aligned` format is the standard, human-readable, nicely formatted text output; this is the default.

`wrapped` format is like aligned but wraps wide data values across lines to make the output fit in the target column width. The target width is determined as described under the columns option. Note that ysqlsh doesn't attempt to wrap column header titles; therefore, `wrapped` format behaves the same as aligned if the total width needed for column headers exceeds the target.

The `html`, `asciidoc`, `latex`, `latex-longtable`, and `troff-ms` formats put out tables that are intended to be included in documents using the respective markup language. They aren't complete documents! This might not be necessary in HTML, but in LaTeX you must have a complete document wrapper. `latex-longtable` also requires the LaTeX longtable and booktabs packages.

### linestyle

Sets the border line drawing style to one of `ascii` or `unicode`. Unique abbreviations are allowed. (That would mean one letter is enough.) The default setting is `ascii`. This option only affects the `aligned` and `wrapped` output formats.

`ascii` style uses plain ASCII characters. Newlines in data are shown using a `+` symbol in the right-hand margin. When the `wrapped` format wraps data from one line to the next without a newline character, a dot (`.`) is shown in the right-hand margin of the first line, and again in the left-hand margin of the following line.

`unicode` style uses Unicode box-drawing characters. Newlines in data are shown using a carriage return symbol in the right-hand margin. When the data is wrapped from one line to the next without a newline character, an ellipsis symbol is shown in the right-hand margin of the first line, and again in the left-hand margin of the following line.

When the `border` setting is greater than `0` (zero), the `linestyle` option also determines the characters with which the border lines are drawn. Plain ASCII characters work everywhere, but Unicode characters look nicer on displays that recognize them.

### null

Sets the string to be printed in place of a null value. The default is to print nothing, which can be mistaken for an empty string. For example, one might prefer `\pset null '(null)'`.

### numericlocale

If *value* is specified, it must be either `on` or `off`, which enables or disables display of a locale-specific character to separate groups of digits to the left of the decimal marker. If *value* is omitted, the command toggles between regular and locale-specific numeric output.

### pager

Controls use of a pager program for query and ysqlsh help output. If the environment variable [PAGER](../ysqlsh/#pager) is set, the output is piped to the specified program. Otherwise, a platform-dependent default (such as `more`) is used.

When the `pager` option is `off`, the pager program isn't used. When the `pager` option is `on`, the pager is used when appropriate; that is, when the output is to a terminal and doesn't fit on the screen. The `pager` option can also be set to `always`, which causes the pager to be used for all terminal output regardless of whether it fits on the screen. `\pset pager` without a *value* toggles pager use `on` and `off`.

### pager_min_lines

If `pager_min_lines` is set to a number greater than the page height, the pager program isn't called unless there are at least this many lines of output to show. The default setting is `0`.

### recordsep

Specifies the record (line) separator to use in unaligned output format. The default is a newline character.

### recordsep_zero

Sets the record separator to use in unaligned output format to a zero byte.

### tableattr (or T)

In `HTML` format, this specifies attributes to be placed inside the `table` tag. This could, for example, be `cellpadding` or `bgcolor`. Note that you probably don't want to specify border here, as that is already taken care of by [`\pset border`](#border). If no value is given, the table attributes are unset.

In `latex-longtable` format, this controls the proportional width of each column containing a left-aligned data type. It is specified as a whitespace-separated list of values, for example, `'0.2 0.2 0.6'`. Unspecified output columns use the last specified value.

### title (or C)

Sets the table title for any subsequently printed tables. This can be used to give your output descriptive tags. If no *value* is given, the title is unset.

### tuples_only (or t)

If *value* is specified, it must be either `on` or `off`, which enables or disables tuples-only mode. If *value* is omitted, the command toggles between regular and tuples-only output. Regular output includes extra information such as column headers, titles, and various footers. In tuples-only mode, only actual table data is shown.

### unicode_border_linestyle

Sets the border drawing style for the `unicode` line style to one of `single` or `double`.

### unicode_column_linestyle

Sets the column drawing style for the `unicode` line style to one of `single` or `double`.

### unicode_header_linestyle

Sets the header drawing style for the `unicode` line style to one of `single` or `double`.
