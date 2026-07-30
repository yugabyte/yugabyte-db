// Initialize orders collection with sample data
print("Initializing orders collection...");

// Switch to the sampledb database
use('sampledb');

// Create orders collection and insert sample orders
db.orders.insertMany([
    {
        _id: "order1",
        orderNumber: "ORD-2024-001",
        userId: "user1",
        customerInfo: {
            email: "alice.smith@example.com",
            firstName: "Alice",
            lastName: "Smith"
        },
        items: [
            {
                productId: "prod1",
                productName: "Wireless Bluetooth Headphones",
                quantity: 1,
                unitPrice: 199.99,
                totalPrice: 199.99
            },
            {
                productId: "prod4",
                productName: "Eco-Friendly Water Bottle",
                quantity: 2,
                unitPrice: 29.99,
                totalPrice: 59.98
            }
        ],
        orderSummary: {
            subtotal: 259.97,
            tax: 20.80,
            shipping: 9.99,
            total: 290.76
        },
        status: "delivered",
        paymentMethod: "credit_card",
        shippingAddress: {
            street: "123 Main St",
            city: "Seattle",
            state: "WA",
            postalCode: "98101",
            country: "USA"
        },
        orderDate: new Date("2024-06-01T10:30:00Z"),
        shippedDate: new Date("2024-06-02T14:15:00Z"),
        deliveredDate: new Date("2024-06-05T16:20:00Z")
    },
    {
        _id: "order2",
        orderNumber: "ORD-2024-002",
        userId: "user2",
        customerInfo: {
            email: "bob.jones@example.com",
            firstName: "Bob",
            lastName: "Jones"
        },
        items: [
            {
                productId: "prod2",
                productName: "Organic Coffee Beans",
                quantity: 3,
                unitPrice: 24.99,
                totalPrice: 74.97
            }
        ],
        orderSummary: {
            subtotal: 74.97,
            tax: 6.00,
            shipping: 5.99,
            total: 86.96
        },
        status: "shipped",
        paymentMethod: "paypal",
        shippingAddress: {
            street: "456 Oak Ave",
            city: "San Francisco",
            state: "CA",
            postalCode: "94102",
            country: "USA"
        },
        orderDate: new Date("2024-06-10T15:45:00Z"),
        shippedDate: new Date("2024-06-11T09:30:00Z")
    },
    {
        _id: "order3",
        orderNumber: "ORD-2024-003",
        userId: "user4",
        customerInfo: {
            email: "david.lee@example.com",
            firstName: "David",
            lastName: "Lee"
        },
        items: [
            {
                productId: "prod3",
                productName: "Smart Fitness Watch",
                quantity: 1,
                unitPrice: 299.99,
                totalPrice: 299.99
            },
            {
                productId: "prod4",
                productName: "Eco-Friendly Water Bottle",
                quantity: 1,
                unitPrice: 29.99,
                totalPrice: 29.99
            }
        ],
        orderSummary: {
            subtotal: 329.98,
            tax: 26.40,
            shipping: 0.00,
            total: 356.38
        },
        status: "processing",
        paymentMethod: "credit_card",
        shippingAddress: {
            street: "789 Pine St",
            city: "Austin",
            state: "TX",
            postalCode: "73301",
            country: "USA"
        },
        orderDate: new Date("2024-06-18T12:15:00Z")
    },
    {
        _id: "order4",
        orderNumber: "ORD-2024-004",
        userId: "user5",
        customerInfo: {
            email: "eve.brown@example.com",
            firstName: "Eve",
            lastName: "Brown"
        },
        items: [
            {
                productId: "prod5",
                productName: "Professional Camera Lens",
                quantity: 1,
                unitPrice: 899.99,
                totalPrice: 899.99
            }
        ],
        orderSummary: {
            subtotal: 899.99,
            tax: 72.00,
            shipping: 15.99,
            total: 987.98
        },
        status: "pending",
        paymentMethod: "bank_transfer",
        shippingAddress: {
            street: "321 Cedar Blvd",
            city: "Portland",
            state: "OR",
            postalCode: "97201",
            country: "USA"
        },
        orderDate: new Date("2024-06-20T14:30:00Z")
    }
]);

print("Created " + db.orders.countDocuments() + " orders in the orders collection");

// Create indexes for better query performance
db.orders.createIndex({ "userId": 1 });
db.orders.createIndex({ "orderNumber": 1 }, { unique: true });
db.orders.createIndex({ "status": 1 });
db.orders.createIndex({ "orderDate": 1 });
db.orders.createIndex({ "customerInfo.email": 1 });

print("Created indexes on orders collection");
