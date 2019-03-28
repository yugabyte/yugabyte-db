
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

### Inline Section Switcher

An inline section switcher lets you switch between content sections **without a separate URL***. If you want to link to sub-sections inside a switcher, use tabs.

You can achieve this as follows:

```
<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="#macos" class="nav-link active" id="macos-tab" data-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fab fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#linux" class="nav-link" id="linux-tab" data-toggle="tab" role="tab" aria-controls="linux" aria-selected="false">
      <i class="fab fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
  <li>
    <a href="#docker" class="nav-link" id="docker-tab" data-toggle="tab" role="tab" aria-controls="docker" aria-selected="false">
      <i class="fab fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
  <li >
    <a href="#kubernetes" class="nav-link" id="kubernetes-tab" data-toggle="tab" role="tab" aria-controls="kubernetes" aria-selected="false">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">
    {{% includeMarkdown "binary/explore-ysql.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/explore-ysql.md" /%}}
  </div>
  <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
    {{% includeMarkdown "docker/explore-ysql.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade" role="tabpanel" aria-labelledby="kubernetes-tab">
    {{% includeMarkdown "kubernetes/explore-ysql.md" /%}}
  </div>
</div>
```

### Information Boxes - NOTE, INFO and TIP

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
