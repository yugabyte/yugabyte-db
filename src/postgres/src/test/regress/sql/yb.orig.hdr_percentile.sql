-- Testing hdr percentile function
SELECT yb_get_percentile('[{"[384.0,409.6)": 5}, {"[768.0,819.2)": 4}, {"[1126.4,1228.8)": 1}]', 50);
SELECT yb_get_percentile('[{"[384.0,409.6)": 5}, {"[768.0,819.2)": 4}, {"[1126.4,1228.8)": 1}]', 90);
SELECT yb_get_percentile('[{"[384.0,409.6)": 5}, {"[768.0,819.2)": 4}, {"[1126.4,1228.8)": 1}]', 99);
SELECT yb_get_percentile('[{"[384.0,409.6)": 5}, {"[768.0,819.2)": 4}, {"[1126.4,1228.8)": 1}]', 0);
SELECT yb_get_percentile('[{"[384.0,409.6)": 5}, {"[768.0,819.2)": 4}, {"[1126.4,1228.8)": 1}]', -0.1);
SELECT yb_get_percentile('[{"[384.0,409.6)": 5}, {"[768.0,819.2)": 4}, {"[1126.4,1228.8)": 1}]', 8892.3);
SELECT yb_get_percentile('[]', 90);
SELECT yb_get_percentile('[{"[-2.8,2e4)": 8}]', -10);
SELECT yb_get_percentile('[{"[-2.8,2e4)": 8}]', 90);
SELECT yb_get_percentile('[{"[-1.1e-3,5000)": 5}, {}]', -10);
SELECT yb_get_percentile('[{"[-1.1e-3,5000)": 5}, {}]', 100);
SELECT yb_get_percentile('[{"[12,)": 8}]', 0);
SELECT yb_get_percentile('[{"[12,)": 8}]', 50);
SELECT yb_get_percentile('[{"[12,)": 8}]', 100);
SELECT yb_get_percentile('[{"[1,2)": 5}, {"[3,4)": 4}, {"[5,)": 1}]', 50);
SELECT yb_get_percentile('[{"[1,2)": 5}, {"[3,4)": 4}, {"[5,)": 1}]', 90);
SELECT yb_get_percentile('[{"[1,2)": 5}, {"[3,4)": 4}, {"[5,)": 1}]', 99);
