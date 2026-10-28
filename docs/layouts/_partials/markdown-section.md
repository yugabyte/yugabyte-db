{{- /* Sections publish at /section/index.md. Also publish a copy at
     /section.md so every docs URL can be fetched by appending .md. */ -}}
{{- $md := partial "markdown-page.md" . -}}
{{- if not .IsHome -}}
  {{- with .OutputFormats.Get "html" -}}
    {{- $path := strings.TrimSuffix "/" (strings.TrimPrefix "/" .RelPermalink) -}}
    {{- if $path -}}
      {{- $copy := resources.FromString (printf "%s.md" $path) $md -}}
      {{- $_ := $copy.Publish -}}
    {{- end -}}
  {{- end -}}
{{- end -}}
{{- $md -}}
