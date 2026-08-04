// Initialize products collection with sample data
print("Initializing products collection...");

// Switch to the test database
use('test');

// Create products collection and insert sample products
db.products.insertMany([
    {
        _id: "prod1",
        name: "Wireless Bluetooth Headphones",
        description: "High-quality wireless headphones with noise cancellation",
        category: "Electronics",
        brand: "AudioTech",
        price: 199.99,
        currency: "USD",
        inStock: true,
        stockQuantity: 150,
        sku: "AT-WBH-001",
        specifications: {
            batteryLife: "30 hours",
            connectivity: ["Bluetooth 5.0", "USB-C"],
            weight: "250g",
            color: "Black"
        },
        ratings: {
            average: 4.5,
            totalReviews: 1247
        },
        tags: ["wireless", "bluetooth", "noise-cancellation"],
        createdAt: new Date("2024-01-10T08:00:00Z"),
        updatedAt: new Date("2024-06-15T12:30:00Z")
    },
    {
        _id: "prod2",
        name: "Organic Coffee Beans",
        description: "Premium organic coffee beans from Colombia",
        category: "Food & Beverages",
        brand: "Mountain Roast",
        price: 24.99,
        currency: "USD",
        inStock: true,
        stockQuantity: 500,
        sku: "MR-OCB-500",
        specifications: {
            origin: "Colombia",
            roastLevel: "Medium",
            weight: "500g",
            organic: true
        },
        ratings: {
            average: 4.8,
            totalReviews: 892
        },
        tags: ["organic", "coffee", "fair-trade"],
        createdAt: new Date("2024-02-01T10:15:00Z"),
        updatedAt: new Date("2024-06-20T09:45:00Z")
    },
    {
        _id: "prod3",
        name: "Smart Fitness Watch",
        description: "Advanced fitness tracker with heart rate monitoring",
        category: "Electronics",
        brand: "FitTech",
        price: 299.99,
        currency: "USD",
        inStock: false,
        stockQuantity: 0,
        sku: "FT-SFW-PRO",
        specifications: {
            display: "1.4 inch AMOLED",
            batteryLife: "7 days",
            waterproof: "5ATM",
            sensors: ["Heart Rate", "GPS", "Accelerometer"]
        },
        ratings: {
            average: 4.3,
            totalReviews: 567
        },
        tags: ["fitness", "smartwatch", "health"],
        createdAt: new Date("2024-03-15T14:20:00Z"),
        updatedAt: new Date("2024-07-01T11:00:00Z")
    },
    {
        _id: "prod4",
        name: "Eco-Friendly Water Bottle",
        description: "Reusable stainless steel water bottle",
        category: "Home & Garden",
        brand: "EcoLife",
        price: 29.99,
        currency: "USD",
        inStock: true,
        stockQuantity: 300,
        sku: "EL-EFWB-750",
        specifications: {
            material: "Stainless Steel",
            capacity: "750ml",
            insulation: "Double Wall",
            bpaFree: true
        },
        ratings: {
            average: 4.6,
            totalReviews: 324
        },
        tags: ["eco-friendly", "reusable", "steel"],
        createdAt: new Date("2024-04-01T09:30:00Z"),
        updatedAt: new Date("2024-06-10T15:45:00Z")
    }
]);

print("Created " + db.products.countDocuments() + " products in the products collection");

// Create indexes for better query performance
db.products.createIndex({ "category": 1 });
db.products.createIndex({ "price": 1 });
db.products.createIndex({ "inStock": 1 });
print("Created indexes on category, price, and inStock fields");