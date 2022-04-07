---
title: Django ORM
linkTitle: Django ORM
description: Django ORM support for YugabyteDB
headcontent: Django ORM support for YugabyteDB
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    name: Python ORMs
    identifier: django-orm
    parent: python-drivers
    weight: 600
isTocNested: true
showAsideToc: true
---
<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/preview/drivers-orms/python/django/" class="nav-link active">
      <i class="icon-java-bold" aria-hidden="true"></i>
      Django ORM
    </a>
  </li>

  <li >
    <a href="/preview/drivers-orms/python/sqlalchemy/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      SQLAlchemy ORM
    </a>
  </li>

</ul>

[Django](https://www.djangoproject.com/) is a high-level Python web framework that encourages rapid development and clean, pragmatic design.

### Add the Dependencies

To download the Django Rest Framework to your project, run the following command:

```shell
pip3 install djangorestframework
```

In addition, install the [YB backend for django](https://github.com/yugabyte/yb-django). Follow the steps on the README to install it. This backend has specific changes with respect to features either not supported by YugabyteDB or supported differently than PostgreSQL. For more information on these features, visit this [GitHub issue](https://github.com/yugabyte/yugabyte-db/issues/7764).

Install the psycopg2 dependency by running the following command:

```sh
pip3 install psycopg2
```

### Implement ORM mapping for YugabyteDB

Once all the dependencies are installed, start a Django project and create a new application. To start the project, run the following command:

```sh
django-admin startproject yugabyteTest && cd yugabyteTest/
```

Set up a new Django application using the following command:

```sh
python manage.py startapp testdb
```

After creating the application, configure the application to connect to the database. To do this, change the application settings to provide the database credentials. In the file `yugabyteTest/settings.py ` add the following code:

```python
DATABASES = {
    'default': {
        'ENGINE': 'yb_backend',
        'NAME': 'yugabyte',
        'HOST': 'localhost',
        'PORT': 5433,
        'USER': 'yugabyte',
        'CONN_MAX_AGE': None,
        'PASSWORD': 'yugabyte'
    }
}
```

For better performance with YugabyteDB, use persistent connections (set `CONN_MAX_AGE`).

You also need the application and rest framework in the INSTALLED_APPS field. Replace the existing code with the following:

```python
INSTALLED_APPS = [
    'rest_framework',
    'testdb.apps.TestdbConfig',
    'django.contrib.contenttypes',
    'django.contrib.auth',
]

REST_FRAMEWORK = {
    'DEFAULT_AUTHENTICATION_CLASSES': [],
    'DEFAULT_PERMISSION_CLASSES': [],
    'UNAUTHENTICATED_USER': None,
}
```

The next step is to create a model for the table. The table name is `users` and contains four columns -`user_id`,`firstName`,`lastName`, and `email`. Add the following code to `testdb/models.py`:

```python
rom django.db import models

class Users(models.Model):
    userId = models.AutoField(db_column='user_id', primary_key=True, serialize=False)
    firstName = models.CharField(max_length=50, db_column='first_name')
    lastName = models.CharField(max_length=50, db_column='last_name')
    email = models.CharField(max_length=100, db_column='user_email')

    class Meta:
        db_table = "users"

    def __str__(self):
        return '%d %s %s %s' % (self.userId, self.firstName, self.lastName, self.email)
```

After creating the model, you need to create a Serializer. Serializers allow complex data such as querysets and model instances to be converted to native Python datatypes that can then be rendered into JSON, XML or other content types. Serializers also provide deserialization, allowing parsed data to be converted back into complex types, after first validating the incoming data. Copy the following code into `testdb\serializers.py`:

```python
from rest_framework import serializers, status
from testdb.models import Users
from django.core.exceptions import ValidationError

class UserSerializer(serializers.ModelSerializer):
    class Meta:
        model = Users
        fields = ('userId', 'firstName', 'lastName', 'email')
```

To finish all the elements of the application, create a ViewSet. In `testdb/views.py`, add the following:

```python
from django.shortcuts import render
from testdb.models import Users
from rest_framework import viewsets
from testdb.serializers import UserSerializer

class UserViewSet(viewsets.ModelViewSet):
    queryset = Users.objects.all()
    serializer_class = UserSerializer
```

Finally map the URLs in `yugabyteTest/urls.py` by adding the following code:

```python
from django.urls import include, re_path
from rest_framework import routers
from testdb.views import UserViewSet

router = routers.SimpleRouter(trailing_slash=False)
router.register(r'users', UserViewSet)
urlpatterns = [
    re_path(r'^', include(router.urls))
]
```

For Django versions earlier than 4.0, use the following code instead, since you can import the URLs using django.conf.urls:

```python
from django.urls import path, include
from django.conf.urls import url, include
from rest_framework import routers
from testdb.views import UserViewSet

router = routers.SimpleRouter(trailing_slash=False)
router.register(r'users', UserViewSet)

urlpatterns = [
    url(r'^', include(router.urls))
]
```

This completes the configuration of your test application. The next steps are to create the migration files and apply the migrations to the database. To do this, run the following command:

```shell
python3 manage.py makemigrations
python3 manage.py migrate
```

A users table should be created in your database. Use the ysqlsh client shell to verify that the users table has been created in your database.

### Run the application

To run the application and insert a new row, execute the following steps.

Run the django project using the following command:

```shell
python3 manage.py runserver 8080
```

Insert a row using the following command:

```shell
$ curl --data '{ "firstName" : "John", "lastName" : "Smith", "email" : "jsmith@yb.com" }' \
       -v -X POST -H 'Content-Type:application/json' http://localhost:8080/users
```

Verify that the new row is inserted by executing the following command:

```shell
$ curl http://localhost:8080/users
```

You should see the following output:

```shell
[{"userId":1,"firstName":"John","lastName":"Smith","email":"jsmith@yb.com"}]
```

You can also verify this using the ysqlsh client shell.

## Next steps

- Explore [Scaling Python Applications](/preview/explore/linear-scalability) with YugabyteDB.
- Learn how to [develop Python applications with Yugabyte Cloud](/preview/yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-ysql-python/).
