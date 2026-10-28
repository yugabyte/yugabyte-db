{{- $url := . -}}
{{- $skip := or
  (eq $url "")
  (strings.HasPrefix $url "/")
  (strings.HasPrefix $url "#")
  (strings.HasPrefix $url "mailto:")
  (strings.HasPrefix $url "http://")
  (strings.HasPrefix $url "https://")
  (strings.HasPrefix $url "data:")
  (strings.HasPrefix $url "{{")
  (findRE `^[a-zA-Z][a-zA-Z0-9+.-]*:` $url)
-}}
{{- if not $skip -}}
yes
{{- end -}}
