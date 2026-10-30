{{- /* Rewrite relative links so they still resolve when the page is fetched at
     page.md. Authored links are relative to the HTML directory URL. */ -}}
{{- $content := .content -}}
{{- $base := .base -}}
{{- $rendered := "" -}}
{{- if not (strings.Contains $content "```") -}}
  {{- $rendered = partial "markdown-rewrite-urls.md" (dict "content" $content "base" $base) -}}
{{- else -}}
  {{- $parts := split $content "```" -}}
  {{- $out := slice -}}
  {{- range $i, $part := $parts -}}
    {{- $piece := $part -}}
    {{- if eq (mod $i 2) 0 -}}
      {{- $piece = partial "markdown-rewrite-urls.md" (dict "content" $part "base" $base) -}}
    {{- end -}}
    {{- $out = $out | append $piece -}}
  {{- end -}}
  {{- $rendered = delimit $out "```" -}}
{{- end -}}
{{- $rendered -}}
