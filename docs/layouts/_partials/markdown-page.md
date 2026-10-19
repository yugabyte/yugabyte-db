{{- /* Markdown body for the .md output format. .Content is the HTML already
     rendered for the page, so shortcodes run once and are shared with the HTML
     output. transform.HTMLToMarkdown needs Hugo 0.151 or newer. */ -}}
{{- $html := .OutputFormats.Get "html" -}}
{{- $canonical := "" -}}
{{- $base := "/" -}}
{{- with $html -}}
  {{- $canonical = .Permalink -}}
  {{- $base = .RelPermalink -}}
{{- end -}}
{{- if not (strings.HasSuffix $base "/") -}}
  {{- $base = printf "%s/" $base -}}
{{- end -}}
{{- $title := .Title -}}
{{- with .Params.headerTitle -}}
  {{- $title = . -}}
{{- end -}}
{{- $body := .Content | transform.HTMLToMarkdown | strings.TrimSpace -}}
{{- $body = partial "markdown-abs-links.md" (dict "content" $body "base" $base) -}}
{{- $desc := "" -}}
{{- with .Description -}}
  {{- $desc = . -}}
{{- end -}}
{{- $lead := "" -}}
{{- with .Params.headcontent -}}
  {{- $lead = . | strings.TrimSpace -}}
{{- end -}}
---
title: {{ $title | jsonify }}
url: {{ $canonical | jsonify }}
---

# {{ $title }}
{{- with $desc }}
{{- if ne . $title }}

{{ . }}
{{- end }}
{{- end }}
{{- with $lead }}
{{- if ne . $desc }}

{{ . }}
{{- end }}
{{- end }}

{{ $body }}
