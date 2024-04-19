# PostgreSQL Anonymizer How To

This is a 4 hours workshop that demonstrates various anonymization techniques.


## Write

This workshop is written with jupyter-notebook. The `*.ipynb` files are mixing
markdown content with live SQL statements that are executed on a PostgreSQL
instance.

```bash
pip install -r requirements.txt
jupyter notebook
```

## Build

The source files are converted to markdown and then exported to pdf, slides,
epub, etc.

```bash
make
```

The export files will be available in the `_build` folder.

Type `make help` for more details
