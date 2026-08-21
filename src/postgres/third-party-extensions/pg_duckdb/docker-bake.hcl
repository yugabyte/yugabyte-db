variable "REPO" {
  default = "pgduckdb/pgduckdb"
}

variable "POSTGRES_VERSION" {
  default = "16"
}

target "shared" {
  platforms = [
    "linux/amd64",
    "linux/arm64"
  ]
}

target "postgres" {
  inherits = ["shared"]

  contexts = {
    postgres_base = "docker-image://postgres:${POSTGRES_VERSION}-bookworm"
  }

  args = {
    POSTGRES_VERSION = "${POSTGRES_VERSION}"
  }

  tags = [
    "${REPO}:${POSTGRES_VERSION}-dev",
  ]
}

target "pg_duckdb" {
  inherits = ["postgres"]
  target = "output"
}

target "pg_duckdb_14" {
  inherits = ["pg_duckdb"]

  args = {
    POSTGRES_VERSION = "14"
  }
}

target "pg_duckdb_15" {
  inherits = ["pg_duckdb"]

  args = {
    POSTGRES_VERSION = "15"
  }
}

target "pg_duckdb_16" {
  inherits = ["pg_duckdb"]

  args = {
    POSTGRES_VERSION = "16"
  }
}

target "pg_duckdb_17" {
  inherits = ["pg_duckdb"]

  args = {
    POSTGRES_VERSION = "17"
  }
}

target "pg_duckdb_18" {
  inherits = ["pg_duckdb"]

  args = {
    POSTGRES_VERSION = "18"
  }
}

target "default" {
  inherits = ["pg_duckdb_18"]
}
