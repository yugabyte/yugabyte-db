// Initialize products collection with sample data
print("Initializing products collection...");

// Switch to the sampledb database
use('sampledb');

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
        createdAt: new Date("2024-02-05T10:15:00Z"),
        updatedAt: new Date("2024-06-10T09:20:00Z")
    },
    {
        _id: "prod3",
        name: "Smart Fitness Watch",
        description: "Advanced fitness tracker with heart rate monitoring and GPS",
        category: "Electronics",
        brand: "FitTrack",
        price: 299.99,
        currency: "USD",
        inStock: true,
        stockQuantity: 75,
        sku: "FT-SFW-002",
        specifications: {
            batteryLife: "7 days",
            waterResistance: "50m",
            sensors: ["Heart Rate", "GPS", "Accelerometer", "Gyroscope"],
            compatibility: ["iOS", "Android"]
        },
        ratings: {
            average: 4.3,
            totalReviews: 654
        },
        tags: ["fitness", "smartwatch", "gps", "health"],
        createdAt: new Date("2024-03-20T14:30:00Z"),
        updatedAt: new Date("2024-06-18T16:45:00Z")
    },
    {
        _id: "prod4",
        name: "Eco-Friendly Water Bottle",
        description: "Sustainable stainless steel water bottle with temperature retention",
        category: "Lifestyle",
        brand: "EcoBottle",
        price: 29.99,
        currency: "USD",
        inStock: true,
        stockQuantity: 300,
        sku: "EB-EFWB-750",
        specifications: {
            material: "Stainless Steel",
            capacity: "750ml",
            insulation: "Double-wall vacuum",
            hotRetention: "12 hours",
            coldRetention: "24 hours"
        },
        ratings: {
            average: 4.6,
            totalReviews: 445
        },
        tags: ["eco-friendly", "stainless-steel", "insulated"],
        createdAt: new Date("2024-04-15T11:00:00Z"),
        updatedAt: new Date("2024-06-12T13:30:00Z")
    },
    {
        _id: "prod5",
        name: "Professional Camera Lens",
        description: "High-quality 85mm portrait lens for professional photography",
        category: "Photography",
        brand: "LensMaster",
        price: 899.99,
        currency: "USD",
        inStock: true,
        stockQuantity: 25,
        sku: "LM-PCL-85",
        specifications: {
            focalLength: "85mm",
            aperture: "f/1.4",
            mount: "Canon EF",
            weight: "950g",
            elements: "11 elements in 8 groups"
        },
        ratings: {
            average: 4.9,
            totalReviews: 178
        },
        tags: ["photography", "lens", "portrait", "professional"],
        createdAt: new Date("2024-05-01T09:30:00Z"),
        updatedAt: new Date("2024-06-20T15:15:00Z")
    }
]);

print("Created " + db.products.countDocuments() + " products in the products collection");

// Create indexes for better query performance
db.products.createIndex({ "category": 1 });
db.products.createIndex({ "brand": 1 });
db.products.createIndex({ "price": 1 });
db.products.createIndex({ "tags": 1 });
db.products.createIndex({ "sku": 1 }, { unique: true });

print("Created indexes on products collection");
