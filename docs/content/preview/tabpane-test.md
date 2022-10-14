---
title: tabpane testing
description: tabpane testing description
headcontent: Testing including a tabpane by reference
weight: 2
type: docs
---

## Tabs on the page

This section uses the `tabpane` shortcode directly.

{{< tabpane code=false >}}
  {{% tab header="Tab 1" lang="tab-1" %}}
  This is **tab 1**
  {{% /tab %}}

  {{% tab header="Tab 2" lang="tab-2" %}}
  This is **tab 2**
  {{% /tab %}}
{{< /tabpane >}}

## Tabs using readfile

This section reads a nearly-identical (only the content of each tab differs: "This is tab..." vs "This is included tab...") `tabpane` shortcode called via `readfile`.

{{< readfile "/static/markdown-snippets/tabpane.md" >}}
