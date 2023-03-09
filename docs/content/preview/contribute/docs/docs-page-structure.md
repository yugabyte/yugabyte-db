---
title: Docs page structure
headerTitle: Docs page structure
linkTitle: Docs page structure
description: Overview of the Markdown structure of YugabyteDB documentation pages
image: /images/section_icons/index/quick_start.png
menu:
  preview:
    identifier: docs-page-structure
    parent: docs-edit
    weight: 2914
type: docs
---

## Structure of a documentation page

All documentation pages must start with frontmatter as shown below.

```yaml
---
title: SEO-Title-Browser-Tab-Title
headerTitle: Doc-Page-Title
linkTitle: Navigation-Link
description: SEO-Meta-Description
headcontent: Brief-description
image: Icon-For-Page-Title
menu:
  preview:
    identifier: page-identifier
    parent: parent-page-identifier
    weight: number-to-decide-display-order
type: docs
showRightNav: true
rightNav:
  showH4: true
---
```

### Mandatory frontmatter attributes

| Field name | Description |
| :--------- | :---------- |
| `title` | Title text to display in browser tab and search engine results. This should be unique to the page. |
| `headerTitle` | Title text to appear as the page title |
| `linkTitle` | Title text to display in the navigation bar and breadcrumbs |
| `description` | Description text to display in search results. This should be unique to the page. |
| `headcontent` | Subtitle text below the headerTitle |
| `menu` | Needs a menu name as defined in `menus.toml`. |
| `identifier` | ID for the page. The page identifier must be unique within the scope of the menu. |
| `parent` | The page's parent in the left nav. Required unless the page is a top-level page. |
| `weight` | Determines menu ordering. Pages of lower weight display higher in the menu. Entries of equal weight are ordered alphabetically. |
| `type` | Must be `docs` or `indexpage`. See [types of pages](#types-of-pages). |

### Optional frontmatter attributes

| Field name | Default | Description |
| :--------: | :-----: | :---------- |
| `image` | N/A | Optional icon displayed next to the title (index pages only) |
| `layout` | (depends) | On pages of `type: indexpage` (where the filename is _not_ `_index.*`), set `layout: list` to get Hugo to render the page `image`. You may also need to set `layout: single` on files of `type: docs` with a name of `_index.*` to force Hugo to render them as regular pages. |
| `showRightNav` | (depends) | Controls display of the TOC on the right. For pages of `type: docs`, default is true. For pages of `type: indexpage`, default is false. |
| `rightNav:` > `hideH4` | `false` | Controls whether H4 elements show up in the TOC on the right. Set this to `true` to show only heading levels 2 and 3 in the right nav. |

## Types of pages

There are two different types of documentation pages: index pages, and content pages.

**Index pages** have links to subtopics in a topic. These pages are generally named `_index.html` or `_index.md`, and have `type: indexpage` in their frontmatter.

**Content pages** contain information about topics. These pages are named in the format `my-docs-page.md`, and have `type: docs` in their frontmatter.
