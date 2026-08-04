import pymongo

from pymongo import MongoClient

# Create a MongoDB client and open a connection to DocumentDB
client = pymongo.MongoClient(
    "mongodb://cloudsa:123456@localhost:10260/?tls=true&tlsAllowInvalidCertificates=true"
)


quickStartDatabase = client["quickStartDatabase"]
quickStartCollection = quickStartDatabase.create_collection("quickStartCollection")


# Insert a single document
quickStartCollection.insert_one(
    {
        "name": "John Doe",
        "email": "john@email.com",
        "address": "123 Main St, Anytown, USA",
        "phone": "555-1234",
    }
)

# Insert multiple documents
quickStartCollection.insert_many(
    [
        {
            "name": "Jane Smith",
            "email": "jane@email.com",
            "address": "456 Elm St, Othertown, USA",
            "phone": "555-5678",
        },
        {
            "name": "Alice Johnson",
            "email": "alice@email.com",
            "address": "789 Oak St, Sometown, USA",
            "phone": "555-8765",
        },
    ]
)


# Read all documents
for document in quickStartCollection.find():
    print(document)

# Read a specific document
singleDocumentReadResult = quickStartCollection.find_one({"name": "John Doe"})
print(singleDocumentReadResult)
