---
title: Linear regression analysis of COVID data
linkTitle: Linear regression on COVID data use-case
headerTitle: Linear regression analysis of COVID data from Carnegie Mellon's COVIDcast project
description: Using the YSQL regr_r2(), regr_slope(), regr_intercept() to examine the correlation between COVID-like symptoms and mask-wearing using data from Carnegie Mellon's COVIDcast
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: covid-data-case-study
    parent: aggregate-functions
    weight: 110
isTocNested: true
showAsideToc: true
---

## Overview

[Carnegie Mellon’s COVIDcast](https://covidcast.cmu.edu/) is an academic project that tracks real-time coronavirus statistics. The team uses various data collection methods and exposes data for download in various formats. This case study uses data that was collected using daily Facebook surveys with the aim of examining the possible correlation between wearing a face-mask and showing symptoms like those of SARS-CoV-2—hereinafter COVID. Specifically, three so-called signals are recorded.

- Does the respondent wear a face mask?

- Does the respondent have COVID-like symptoms?
- Does the respondent know someone in their community  who has COVID-like symptoms?

Each signal is expressed as a percentage relative to the number of people who answered the particular question.

The download format, for each  signal, is a comma-separated values file—hereinafter `.csv` file. The download page says this:

> We are happy for you to use this data in products and publications. Please acknowledge us as a source: Data from Delphi COVIDcast, [covidcast.cmu.edu](https://covidcast.cmu.edu/).

This case study shows you how to use the `ysqlsh` `\COPY` metacommand to load each downloaded file into its own table, how to check that the values conform to rules that the COVIDcast team has documented, and how to join the rows in these staging tables into a single table with this format:

```
survey_date                 date     not null } primary
state                       text     not null }   key
mask_wearing_pct            numeric  not null
mask_wearing_stderr         numeric  not null
mask_wearing_sample_size    integer  not null
symptoms_pct                numeric  not null
symptoms_stderr             numeric  not null
symptoms_sample_size        integer  not null
cmnty_symptoms_pct          numeric  not null
cmnty_symptoms_stderr       numeric  not null
cmnty_symptoms_sample_size  integer  not null
```

It then shows you how to use the linear-regression functions [`regr_r2()`](../function-syntax-semantics/linear-regression/regr/#regr-r2), [`regr_slope()`](../function-syntax-semantics/linear-regression/regr/#regr-slope-regr-intercept), and [`regr_intercept()`](../function-syntax-semantics/linear-regression/regr/#regr-slope-regr-intercept) to examine the correlation between mask-wearing and COVID-like symptoms. The section [Functions for linear regression analysis](../function-syntax-semantics/linear-regression/) explains the general background for these functions.

The remaining account of this case-study is divided into three parts:

- [How to find and download the COVIDcast data](./download-the-covidcast-data/)
- [How to ingest the data](./ingest-the-covidcast-data/), check that the values conform to  the rules that the COVIDcast team has documented, and to transform these into the single _"covidcast_fb_survey_results"_ table.
- [How to use YSQL aggregate functions to examine the possible correlation](./analyze-the-covidcast-data/) between wearing a face-mask and showing COVID-like symptoms.

{{< tip title="Download a zip of all the files that this case study uses" >}}

All of the `.sql` scripts that this case-study presents for copy-and-paste at the `ysqlsh` prompt are included for download in a zip-file. The zip also includes the three `csv` files that you will download from the <a href="https://covidcast.cmu.edu/" target="_blank">Carnegie Mellon’s COVIDcast <i class="fas fa-external-link-alt"></i></a> site. This will allow you, after you've studied the account of the case study and run the files one by one, then to run everything by starting a single master script that will ingest the data and spool the reports the this study explains to files. It will allow you easily to re-run the analysis on newer data as it becomes available.

It is expected that the raw data will be available from the COVIDcast site into the indefinite future. But the downloadable self-contained zip-fie of the complete case study assures readers of the longevity of this study's pedagogy.

After unzipping it on a convenient new directory, you'll see a `README.txt`. It tells you to run `0.sql`. You'll see this in the top directory. It looks like this:

```plpgsql
\i ingest-the-data.sql
\i analysis-queries.sql
\i synthetic-data.sql
```

Simply start it in ysqlsh. You can run it time and again. It always finishes silently. You can see the reports that it produces on the _"analysis-results"_ directory and confirm that the the files that are spooled are identical to the corresponding reference copies that are delivered in the zip-file.
{{< /tip >}}