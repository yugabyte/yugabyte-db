// Initialize users collection with sample data
print("Initializing users collection...");

// Switch to the sampledb database
use('sampledb');

// Create users collection and insert sample users
db.users.insertMany([
    {
        _id: "user1",
        username: "alice_smith",
        email: "alice.smith@example.com",
        firstName: "Alice",
        lastName: "Smith",
        age: 28,
        city: "Seattle",
        country: "USA",
        isActive: true,
        createdAt: new Date("2024-01-15T10:30:00Z"),
        preferences: {
            newsletter: true,
            notifications: true,
            theme: "dark"
        },
        tags: ["premium", "early_adopter"]
    },
    {
        _id: "user2",
        username: "bob_jones",
        email: "bob.jones@example.com",
        firstName: "Bob",
        lastName: "Jones",
        age: 35,
        city: "San Francisco",
        country: "USA",
        isActive: true,
        createdAt: new Date("2024-02-20T14:15:00Z"),
        preferences: {
            newsletter: false,
            notifications: true,
            theme: "light"
        },
        tags: ["standard"]
    },
    {
        _id: "user3",
        username: "carol_wilson",
        email: "carol.wilson@example.com",
        firstName: "Carol",
        lastName: "Wilson",
        age: 42,
        city: "New York",
        country: "USA",
        isActive: false,
        createdAt: new Date("2024-03-10T09:45:00Z"),
        preferences: {
            newsletter: true,
            notifications: false,
            theme: "light"
        },
        tags: ["premium", "vip"]
    },
    {
        _id: "user4",
        username: "david_lee",
        email: "david.lee@example.com",
        firstName: "David",
        lastName: "Lee",
        age: 31,
        city: "Austin",
        country: "USA",
        isActive: true,
        createdAt: new Date("2024-04-05T16:20:00Z"),
        preferences: {
            newsletter: true,
            notifications: true,
            theme: "dark"
        },
        tags: ["standard", "developer"]
    },
    {
        _id: "user5",
        username: "eve_brown",
        email: "eve.brown@example.com",
        firstName: "Eve",
        lastName: "Brown",
        age: 26,
        city: "Portland",
        country: "USA",
        isActive: true,
        createdAt: new Date("2024-05-12T11:00:00Z"),
        preferences: {
            newsletter: true,
            notifications: false,
            theme: "auto"
        },
        tags: ["premium", "beta_tester"]
    }
]);

print("Created " + db.users.countDocuments() + " users in the users collection");

// Create indexes for better query performance
db.users.createIndex({ "email": 1 }, { unique: true });
db.users.createIndex({ "username": 1 }, { unique: true });
db.users.createIndex({ "city": 1 });
db.users.createIndex({ "tags": 1 });

print("Created indexes on users collection");
