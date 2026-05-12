# Data Initialization Support

DocumentDB supports two initialization modes when the emulator container starts:

* Built-in sample data for quick exploration (enabled by default)
* Custom JavaScript initialization scripts supplied by the user

## Environment Variables

The entrypoint honors the following environment variables:

- `INIT_DATA_PATH`: Directory containing `.js` initialization files (default: `/init_doc_db.d`).
- `SKIP_INIT_DATA`: Set to `true` to skip loading built-in sample data (default: `false`).

> Note: When custom initialization is requested with `--init-data-path`, the entrypoint internally sets `SKIP_INIT_DATA=true` so that only the provided scripts run.

## Command Line Options

- `--init-data-path [PATH]`: Execute all `.js` files in the specified directory (alphabetical order) using `mongosh`.
- `--skip-init-data`: Skip loading the built-in sample collections.

If no option is supplied, the emulator starts with the built-in sample data.

## Usage Examples

### Default startup (built-in sample data)
```bash
docker run -p 10260:10260 -p 9712:9712 \
  --password mypassword \
  documentdb/local
```

### Skip sample data entirely
```bash
docker run -p 10260:10260 -p 9712:9712 \
  --skip-init-data \
  --password mypassword \
  documentdb/local
```

### Use custom initialization scripts
```bash
docker run -p 10260:10260 -p 9712:9712 \
  -v /path/to/your/init/scripts:/init_doc_db.d \
  --init-data-path /init_doc_db.d \
  --password mypassword \
  documentdb/local
```

### Configure via environment variables
```bash
docker run -p 10260:10260 -p 9712:9712 \
  -e INIT_DATA_PATH=/custom/init/path \
  -e SKIP_INIT_DATA=true \
  -e PASSWORD=mypassword \
  -v /path/to/your/init/scripts:/custom/init/path \
  documentdb/local
```

## Built-in Sample Data

Unless `--skip-init-data` (or `SKIP_INIT_DATA=true`) is supplied, the following collections are created in the `sampledb` database:
- users (5 sample users)
- products (5 sample products)
- orders (4 sample orders)
- analytics (sample metrics and activity data)

## Security Note

Built-in sample data is intended for evaluation scenarios. Disable it via `--skip-init-data` / `SKIP_INIT_DATA=true` and provide vetted scripts through `INIT_DATA_PATH` for production workloads.
