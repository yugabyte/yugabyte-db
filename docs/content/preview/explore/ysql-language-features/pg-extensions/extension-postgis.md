---
title: PostGIS extension
headerTitle: PostGIS extension
linkTitle: PostGIS
description: Using the PostGIS extension in YugabyteDB
menu:
  preview:
    identifier: extension-postgis
    parent: pg-extensions
    weight: 20
type: docs
---

The [PostGIS](https://postgis.net) PostgreSQL extension adds support for storing, indexing, and querying geographic data.

Note: YSQL does not currently support GiST indexes. This is tracked in [GitHub issue #1337](https://github.com/yugabyte/yugabyte-db/issues/1337).

## Install PostGIS

### macOS

There are two ways to install PostGIS on macOS:

* Download and install [Postgres.app](https://postgresapp.com/)

* Or, install with Homebrew:

    ```sh
    brew install postgres postgis
    ```

### Ubuntu

Add the [PostgreSQL APT sources](https://www.postgresql.org/download/linux/ubuntu/). Then, use `apt` to install:

```shell script
sudo apt-get install postgresql-11 postgresql-11-postgis-3
```

### CentOS

Get the YUM repository from the [PostgreSQL website](https://www.postgresql.org/download/linux/redhat/). Then, use `yum` or `dnf` to install:

```sh
sudo yum install postgresql11-server postgis31_11 postgis31_11-client
```

## Install the extension

Copy the extension files to your YugabyteDB installation as follows:

```sh
cp -v "$(pg_config --pkglibdir)"/*postgis*.so "$(yb_pg_config --pkglibdir)" &&
cp -v "$(pg_config --sharedir)"/extension/*postgis*.sql "$(yb_pg_config --sharedir)"/extension &&
cp -v "$(pg_config --sharedir)"/extension/*postgis*.control "$(yb_pg_config --sharedir)"/extension
```

On Linux systems, PostGIS libraries have dependencies that must also be installed. Use the extensions option of the post-install tool, available in YugabyteDB 2.3.2 and later, as follows:

```sh
./bin/post_install.sh -e
```

Then, create the extension:

```sh
./bin/ysqlsh -c "CREATE EXTENSION postgis;"
```

This may take a couple of minutes.

## Example

1. Get a sample [PostGIS dataset](https://data.edmonton.ca/Geospatial-Boundaries/City-of-Edmonton-Neighbourhood-Boundaries/jfvj-x253):

    ```sh
    wget -O edmonton.zip "https://data.edmonton.ca/api/geospatial/jfvj-x253?method=export&format=Shapefile" && unzip edmonton.zip
    ```

1. Extract the dataset using the `shp2pgsql` tool. This should come with your PostgreSQL installation — it is not yet packaged with YSQL.

    ```sh
    shp2pgsql geo_export_*.shp > edmonton.sql
    ```

1. Edit the generated `edmonton.sql` for YSQL compatibility.

    * First, inline the `PRIMARY KEY` declaration for `gid` as YSQL does not yet support adding primary key constraints after the table creation.
    * Additionally, for simplicity, change the table name (and references to it in the associated `INSERT` statements) to just `geo_export` (in other words, remove the UUID postfix).

    The `edmonton.sql` file should now start as follows:

    ```sql
    SET CLIENT_ENCODING TO UTF8;
    SET STANDARD_CONFORMING_STRINGS TO ON;
    BEGIN;
    CREATE TABLE "geo_export" (gid serial PRIMARY KEY,
      "area_km2" numeric,
      "name" varchar(254),
      "number" numeric);
    SELECT AddGeometryColumn('','geo_export','geom','0','MULTIPOLYGON',2);

    INSERT INTO "geo_export" ("area_km2","name","number",geom) VALUES ...
    ```

1. Load the sample data.

    ```sh
    ./bin/ysqlsh -a -f edmonton.sql
    ```

1. Run some sample queries. Connect using `ysqlsh` and run the following:

    ```sql
    SELECT name, area_km2, ST_Area(geom), ST_Area(geom)/area_km2 AS area_ratio FROM "geo_export" LIMIT 10;
    ```

    ```output
                name            |     area_km2      |       st_area        |      area_ratio
    ----------------------------+-------------------+----------------------+----------------------
    River Valley Terwillegar   | 3.077820277027079 | 0.000416617423004673 | 0.000135361192501822
    Carleton Square Industrial | 0.410191631391664 | 5.56435079305678e-05 | 0.000135652469899947
    Cy Becker                  | 1.015144841249301 | 0.000137900847258255 | 0.000135843518732308
    Elsinore                   | 0.841471068786406 | 0.000114331091817771 |  0.00013587049639468
    McLeod                     | 0.966538217483227 | 0.000131230296771637 | 0.000135773520796051
    Gainer Industrial          | 0.342464541730177 | 4.63954326887451e-05 | 0.000135475142782225
    Coronet Industrial         | 1.606907195063447 | 0.000217576340986435 | 0.000135400688760899
    Marquis                    | 9.979100854886905 |  0.00135608901739072 | 0.000135892906295924
    South Terwillegar          | 1.742840325820606 | 0.000235695089933611 | 0.000135236192576985
    Carlisle                   | 0.961897333826841 | 0.000130580966739925 | 0.000135753538499185
    (10 rows)
    ```

    ```sql
    SELECT a.name, b.name FROM "geo_export" AS a, "geo_export" AS b
    WHERE ST_Intersects(a.geom, b.geom) AND a.name LIKE 'University of Alberta';
    ```

    ```output
            name          |          name
    -----------------------+-------------------------
    University of Alberta | University of Alberta
    University of Alberta | McKernan
    University of Alberta | Belgravia
    University of Alberta | Garneau
    University of Alberta | River Valley Mayfair
    University of Alberta | River Valley Walterdale
    University of Alberta | Windsor Park
    (7 rows)
    ```
