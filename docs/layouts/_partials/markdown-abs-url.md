{{- $url := .url -}}
{{- $base := .base -}}
{{- $parts := index (findRESubmatch `^([^?#]*)(.*)$` $url) 0 -}}
{{- $path := index $parts 1 -}}
{{- $suffix := "" -}}
{{- if ge (len $parts) 3 -}}
  {{- $suffix = index $parts 2 -}}
{{- end -}}
{{- if eq $path "" -}}
{{- $url -}}
{{- else -}}
{{- $abs := path.Join $base $path -}}
{{- if and (strings.HasSuffix $path "/") (not (strings.HasSuffix $abs "/")) -}}
  {{- $abs = printf "%s/" $abs -}}
{{- end -}}
{{- printf "%s%s" $abs $suffix -}}
{{- end -}}
