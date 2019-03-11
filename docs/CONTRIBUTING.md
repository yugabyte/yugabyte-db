
# Structure of each page

All docs pages must start with a front matter as shown below.

```
---
title: Browser-Title
linkTitle: My-Left-Nav-Link
description: Doc-Page-Title
image: Icon-For-Page-Title
headcontent: Brief-description
menu:
  latest:
    identifier: page-identifier
    parent: parent-page-identifier
    weight: number-to-decide-display-order
---
```
## Mandatory Front Matter Attributes

| Field Name      | Description           |
| :-------------: | --------------------- |
| `title`         | Page title displayed in browser tab |
| `linkTitle`     | Text displayed in left navigation |
| `description`   | Text displayed as the title of the docs page |

## Optional Front Matter Attributes

| Field Name      | Default | Description           |
| :-------------: | :-----: | --------------------- |
| `image`         | -       | Optional icon that is displayed next to the title |
| `isTocNested`   | `false` | Should sub-sections be displayed in the TOC on the right |
| `showAsideToc`  | `false` | Should the TOC on the right be enabled |
| `hidePagination`| `false` | Should the automatic navigation links be displayed at the bottom of the page |


# Types of Pages

There are different types of docs pages as noted below.

## Index Pages

Index pages have links to various sub-topics inside a topic. These pages are named `_index.html`. 

## Content Pages

Content pages contain information about topics. The names of these docs page has the format `my-docs-page.md`.

# Widgets

There are a number of display widgets available. These are listed below.

### NOTE, INFO and TIP Boxes

Short code for a note box:
```
{{< note title="Note" >}}
This is a note with a [link](https://www.yugabyte.com).
{{< /note >}}
```

Short code for a tip code:
```
{{< tip title="Tip" >}}
This is a tip with a [link](https://www.yugabyte.com).
{{< /tip >}}
```
