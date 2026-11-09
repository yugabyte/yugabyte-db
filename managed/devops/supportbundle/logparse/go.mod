module github.com/yugabyte/yugabyte-db/managed/devops/supportbundle/logparse

go 1.22

// The toolchain node-agent requires, so every host builds the helpers with the same Go.
toolchain go1.26.6

require github.com/dlclark/regexp2 v1.11.5
