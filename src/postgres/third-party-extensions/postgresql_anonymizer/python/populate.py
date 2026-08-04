#!/usr/bin/python3

import sys
import csv
import argparse
import random
import faker
import importlib


def address():
    return [[oid, f.unique.address().replace('\n', ', ')]
            for oid in range(lines)]


def city():
    return [[oid, f.unique.city()] for oid in range(lines)]


def company():
    return [[oid, f.unique.company()] for oid in range(lines)]


# The dataset is too small, we're extracting the values directly
def country():
    values = []
    for loc in locales:
        m = importlib.import_module('faker.providers.address.'+loc)
        values += list(set(m.Provider.countries))
    random.shuffle(values)
    return [[oid, values[oid]]
            for oid in range(min(len(values), lines))]


def email():
    return [[oid, f.unique.email()] for oid in range(lines)]


# The dataset is too small, we're extracting the values directly
def first_name():
    values = []
    for loc in locales:
        m = importlib.import_module('faker.providers.person.'+loc)
        values += list(set(m.Provider.first_names))
    random.shuffle(values)
    return [[oid, values[oid]]
            for oid in range(min(len(values), lines))]


def iban():
    return [[oid, f.unique.iban()] for oid in range(lines)]


# The dataset is too small, we're extracting the values directly
def last_name():
    values = []
    for loc in locales:
        m = importlib.import_module('faker.providers.person.'+loc)
        values += list(set(m.Provider.last_names))
    random.shuffle(values)
    return [[oid, values[oid]]
            for oid in range(min(len(values), lines))]


def lorem_ipsum():
    return [[oid, f.unique.paragraph(nb_sentences=8)] for oid in range(lines)]


def postcode():
    return [[oid, f.unique.postcode()] for oid in range(lines)]


def siret():
    # override the locales this data is only relevant in France
    french_faker = faker.Faker('fr_FR')
    return [[oid, french_faker.unique.siret()] for oid in range(lines)]


generator_methods = [
  'address', 'city', 'company', 'country', 'email', 'first_name', 'iban',
  'last_name', 'lorem_ipsum', 'postcode', 'siret'
]


# Input
parser = argparse.ArgumentParser()
parser.add_argument(
    '--table',
    help='Type of data ({})'.format(generator_methods),
    choices=generator_methods,
    required=True
)
parser.add_argument(
    '--locales',
    help='Localization of the fake data (comma separated list)',
    default='en'
)
parser.add_argument(
    '--lines',
    help='Number of rows to add to the table',
    type=int,
    default=1000
)
parser.add_argument(
    '--seed',
    help='Initializes the random generator'
)
args = parser.parse_args()

locales = args.locales.split(',')
lines = args.lines

# Generator
f = faker.Faker(locales)
if args.seed:
    random.seed(args.seed)
    faker.Faker.seed(args.seed)

for row in locals().get(args.table)():
    csv.writer(sys.stdout, delimiter='\t').writerow(row)
