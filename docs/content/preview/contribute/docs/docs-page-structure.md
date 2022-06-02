---
title: Docs page structure
headerTitle: Docs page structure
linkTitle: Docs page structure
description: Docs page structure
image: /images/section_icons/index/quick_start.png
type: page
menu:
  preview:
    identifier: docs-page-structure
    parent: docs-edit
    weight: 2914
isTocNested: true
showAsideToc: true
---

## Structure of a documentation page

All documentation pages must start with frontmatter as shown below.

```yaml
---
title: SEO-Title-Browser-Tab-Title
headerTitle: Doc-Page-Title
linkTitle: Navigation-Link
description: SEO-Meta-Description
image: Icon-For-Page-Title
headcontent: Brief-description
menu:
  preview:
    identifier: page-identifier
    parent: parent-page-identifier
    weight: number-to-decide-display-order
isTocNested: true
showAsideToc: true
---
```

### Mandatory frontmatter attributes

| Field name | Description |
| :--------- | :---------- |
| `title` | Title text to display in browser tab and search engine results |
| `headerTitle` | Title text to appear as the page title |
| `linkTitle` | Title text to display in the navigation bar |
| `description` | Description text to display in search engine results |
| `headcontent` | Subtitle text below the headerTitle (Index pages only) |

### Optional frontmatter attributes

| Field name | Default | Description |
| :--------: | :-----: | :---------- |
| `image` | N/A | Optional icon displayed next to the title |
| `isTocNested` | `false` | Should sub-sections be displayed in the TOC on the right |
| `showAsideToc` | `false` | Should the TOC on the right be enabled. In most cases, set to true. |
| `hidePagination`| `false` | Should the automatic navigation links be displayed at the bottom of the page |

## Types of pages

There are two different types of documentation pages: index pages, and content pages.

**Index pages** have links to subtopics in a topic. These pages are named `_index.html` or `_index.md`.

**Content pages** contain information about topics. These pages are named in the format `my-docs-page.md`.
