{{- /* Sections that set layout: single still need the /section.md copy. */ -}}
{{- if .IsNode -}}
{{- partial "markdown-section.md" . -}}
{{- else -}}
{{- partial "markdown-page.md" . -}}
{{- end -}}
