{{- define "byoc-api-proxy.fullname" -}}
{{- printf "%s-byoc-api-proxy" .Release.Name | trunc 63 | trimSuffix "-" -}}
{{- end -}}

{{- define "byoc-api-proxy.selectorLabels" -}}
app.kubernetes.io/name: byoc-api-proxy
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end -}}

{{- define "byoc-api-proxy.labels" -}}
{{ include "byoc-api-proxy.selectorLabels" . }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
helm.sh/chart: {{ printf "%s-%s" .Chart.Name .Chart.Version | replace "+" "_" }}
{{- with .Values.commonLabels }}
{{ toYaml . }}
{{- end }}
{{- end -}}

{{- define "byoc-api-proxy.secretName" -}}
{{- .Values.proxiedApp.auth.existingSecret | default (include "byoc-api-proxy.fullname" .) -}}
{{- end -}}

{{- define "byoc-api-proxy.ybaBaseUrl" -}}
{{- .Values.yba.baseUrl | default (printf "http://%s-yugaware-ui/api" .Release.Name) -}}
{{- end -}}

{{- define "byoc-api-proxy.validateAuthType" -}}
{{- if not (has .Values.proxiedApp.auth.type (list "api_key" "service_account")) -}}
{{- fail (printf "proxiedApp.auth.type must be api_key or service_account, got '%s'" .Values.proxiedApp.auth.type) -}}
{{- end -}}
{{- end -}}
