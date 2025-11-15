<!--
+++
private = true
block_indexing = true
+++
-->

### includeMarkdown

This content is included from a separate file (called, in this case, `include-markdown.md`) using an `includeMarkdown` shortcode.

Use includeMarkdown to add repetitive text to multiple pages.

Headings in included markdown files _do_ appear in the right navigation.

However, shortcodes _do not_ render correctly:

{{<yb-version version="stable">}}

#### Another heading in the includeMarkdown

This heading does appear in the right navigation.
