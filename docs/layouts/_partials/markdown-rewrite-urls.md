{{- $content := .content -}}
{{- $base := .base -}}
{{- /* Markdown links, including ones with a title after the URL. The character
     after the URL is ) or a space, so a shorter URL is not a prefix of a longer one. */ -}}
{{- range findRESubmatch `\]\(([^)\s]+)` $content -}}
  {{- $url := index . 1 -}}
  {{- if eq (partial "markdown-url-is-relative.md" $url | strings.TrimSpace) "yes" -}}
    {{- $abs := partial "markdown-abs-url.md" (dict "base" $base "url" $url) | strings.TrimSpace -}}
    {{- if and $abs (ne $abs $url) -}}
      {{- $content = replace $content (printf "](%s)" $url) (printf "](%s)" $abs) -}}
      {{- $content = replace $content (printf "](%s " $url) (printf "](%s " $abs) -}}
    {{- end -}}
  {{- end -}}
{{- end -}}
{{- range findRESubmatch `(?:href|src)="([^"]+)"` $content -}}
  {{- $full := index . 0 -}}
  {{- $url := index . 1 -}}
  {{- if eq (partial "markdown-url-is-relative.md" $url | strings.TrimSpace) "yes" -}}
    {{- $abs := partial "markdown-abs-url.md" (dict "base" $base "url" $url) | strings.TrimSpace -}}
    {{- if and $abs (ne $abs $url) -}}
      {{- $content = replace $content $full (replace $full $url $abs) -}}
    {{- end -}}
  {{- end -}}
{{- end -}}
{{- $content = replaceRE `\[\]\([^)\n]*\)` "" $content -}}
{{- $content -}}
