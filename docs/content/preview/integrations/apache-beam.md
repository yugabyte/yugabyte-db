---
title: Apache Beam
linkTitle: Apache Beam
description: Apache Beam
menu:
  preview_integrations:
    identifier: apache-beam
    parent: data-integration
    weight: 571
type: docs
---

[Apache Beam](https://beam.apache.org/) is an open source, unified model for defining both batch and streaming data-parallel processing pipelines. Using one of the open source Beam SDKs, you build a program that defines the pipeline. The pipeline is then executed by one of Beam’s supported distributed processing back-ends, which include [Apache Flink](https://flink.apache.org/), [Apache Spark](https://spark.apache.org/), and [Google Cloud Dataflow](https://cloud.google.com/dataflow).

## Prerequisite

To use Apache Beam, ensure that you have YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../quick-start/).

## Setup

To run Apache Beam with YugabyteDB, do the following:

1. Create a folder `apache-beam-test` as follows:

    ```sh
    mkdir apache-beam-test && cd apache-beam-test
    ```

1. Create a python virtual environment and activate it as follows:

   ```python
   python3 -m venv myenv
   source myenv/bin/activate
   ```

1. Install the latest Apache Beam Python SDK from Pypi using the following command:

    ```python
    pip install beam-nuggets
    ```

1. Create a python file, `democode.py` and add the following code to it:

    ```python
    import apache_beam as beam
    from apache_beam.options.pipeline_options import PipelineOptions
    from beam_nuggets.io import relational_db
    records = [
        {'name': 'Jan', 'num': 1},
        {'name': 'Feb', 'num': 2},
        {'name': 'Mar', 'num': 3},
        {'name': 'Apr', 'num': 4},
        {'name': 'May', 'num': 5},
    ]
    source_config = relational_db.SourceConfiguration(
        drivername='postgresql',  #postgresql+pg8000
        host='127.0.0.1',
        port=5433,
        username=’yugabyte’,
        password='yugabyte',
        database='yugabyte',
        create_if_missing=True  # create the database if not there
    )
    table_config = relational_db.TableConfiguration(
        name='months_col01',
        create_if_missing=True,
        primary_key_columns=['num']
    )
    with beam.Pipeline(options=PipelineOptions()) as p:
        months = p | "Reading month records" >> beam.Create(records)
        months | 'Writing to DB table' >> relational_db.Write(
            source_config=source_config,
            table_config=table_config
        )
    if __name__ == "__main__":
        print('demo code ran successful')
    ```

1. Run `democode.py` as follows:

    ```python
    python democode.py
    ```

    ```output
    demo code ran successful
    ```

1. You can also verify the changes from a [ysql shell](../../admin/ysqlsh/#starting-ysqlsh) as follows:

    ```sql
    select * from month_col01;
    ```

    You can see the following output:

    ```output
     num | name
    -----+------
       5 | May
       1 | Jan
       4 | Apr
       2 | Feb
       3 | Mar
    (5 rows)
    ```
