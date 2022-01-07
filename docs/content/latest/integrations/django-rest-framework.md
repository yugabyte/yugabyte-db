---
title: Django REST framework
linkTitle: Django REST framework
description: Django REST framework
menu:
  latest:
    identifier: django-rest-framework
    parent: integrations
    weight: 670
isTocNested: true
showAsideToc: true
---

This document describes how to use [Django REST framework](https://www.django-rest-framework.org/), an ORM library in Python with YugabyteDB.

## Prerequisites

- Install YugabyteDB and start a single node local cluster. Refer [YugabyteDB Quick start](../../quick-start/) to install and start a local cluster.
- Install [Python3](https://www.python.org/downloads/).
- Install [Django backend for YugabyteDB](https://github.com/yugabyte/yb-django).
- Install Django REST framework.

```sh
pip3 install djangorestframework
```

- Install `psycopg2`.

```sh
pip3 install psycopg2
```

## Use Django REST framework

You can start using Django REST framework with YugabyteDB as follows:

- Create a new Django project with the command:

```python
django-admin startproject yugabyteTest && cd yugabyteTest/
```

- Set up a new Django application with the command:

```python
python manage.py startapp testdb
```

- Create a model by adding the following code in `testdb/models.py`:

```python
from django.db import models

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

- Create a [Serializer](https://www.django-rest-framework.org/api-guide/serializers/) in  `testdb\serializers.py` which manages serialization and deserialization from JSON and add the following code to it:

```python
from rest_framework import serializers, status
from testdb.models import Users
from django.core.exceptions import ValidationError

class UserSerializer(serializers.ModelSerializer):
    class Meta:
        model = Users
        fields = ('userId', 'firstName', 'lastName', 'email')
```

- Create a [ViewSet](https://www.django-rest-framework.org/api-guide/viewsets/) in `testdb/views.py` and add the following code:

```python
from django.shortcuts import render
from testdb.models import Users
from rest_framework import viewsets
from testdb.serializers import UserSerializer

class UserViewSet(viewsets.ModelViewSet):
    queryset = Users.objects.all()
    serializer_class = UserSerializer
```

The application now has all the elements and is ready to be configured as follows:

- Map the URLs in `yugabyteTest/urls.py` by adding the following code:

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

For Django versions below 4.0 add the following code in `urls.py` instead, since you can import the urls using `django.conf.urls`:

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

- Configure the `yugabyteTest/settings.py` to use YugabyteDB by updating the value of DATABASES field with:

```python
DATABASES = {
    'default': {
        'ENGINE': 'yb_backend',
        'NAME': 'yugabyte',
        'HOST': 'localhost',
        'PORT': 5433,
        'USER': 'yugabyte',
        'PASSWORD': 'yugabyte'
    }
}
```

You also need the application and rest framework in the `INSTALLED_APPS` field. Replace the existing code with:

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

- Create the migrations with the command:

```python
python3 manage.py makemigrations
```

- Migrate the changes to the database with the command:

```python
python3 manage.py migrate
```

- A users table should be created in your database. Verify that the table is created with the [ysqlsh](../../explore/ysql-language-features/databases-schemas-tables/#list-tables) client shell.

## Run the application

- Run the django project with the command:

```python
python3 manage.py runserver 8080
```

- Insert a row with the command:

```sh
$ curl --data '{ "firstName" : "John", "lastName" : "Smith", "email" : "jsmith@yb.com" }' \
       -v -X POST -H 'Content-Type:application/json' http://localhost:8080/users
```

- Verify that the new row is inserted with the command:

```sh
$ curl http://localhost:8080/users
```

```output
[{"userId":1,"firstName":"John","lastName":"Smith","email":"jsmith@yb.com"}]
```

Alternatively, you can verify it in the [ysqlsh](../../explore/ysql-language-features/databases-schemas-tables/#describe-a-table) client shell.
