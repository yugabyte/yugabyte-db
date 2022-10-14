---
title: tabpane testing
description: tabpane testing description
headcontent: Testing including a tabpane by reference
weight: 2
type: docs
---

## Tabs on the page

This section uses the `tabpane` shortcode directly. Click a tab to change the content in this pane and the one that follows.

{{< tabpane code=false >}}
  {{% tab header="Tab 1" lang="tab-1" %}}
  This is **tab 1**
  {{% /tab %}}

  {{% tab header="Tab 2" lang="tab-2" %}}
  This is **tab 2**
  {{% /tab %}}
{{< /tabpane >}}

This second tabpane will control the one above and vice versa, BUT neither of the bottom two panes will control this one at all.

{{< tabpane code=false >}}
  {{% tab header="Tab 3" lang="tab-1" %}}
  This is **tab 3**
  {{% /tab %}}

  {{% tab header="Tab 4" lang="tab-2" %}}
  This is **tab 4**
  {{% /tab %}}
{{< /tabpane >}}

## Tabs using readfile

This section reads a nearly-identical (only the content of each tab differs: "This is tab..." vs "This is included tab...") `tabpane` shortcode called via `readfile`.

The first included tabpane controls the _content_ (but not the _highlighted state_ of the tab itself) of the first tabpane up top.

{{< readfile "/static/markdown-snippets/tabpane.md" >}}

The second included tabpane does the same.

{{< readfile "/static/markdown-snippets/tabpane.md" >}}
