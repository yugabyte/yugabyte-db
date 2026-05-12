### Join data from multiple collections

Let's create an additional collection named `appointment` to demonstrate how a join operation can be performed.

```sql
select documentdb_api.insert_one('documentdb','appointment', '{"appointment_id": "A001", "patient_id": "P001", "doctor_name": "Dr. Milind", "appointment_date": "2023-01-20", "reason": "Routine checkup" }');
select documentdb_api.insert_one('documentdb','appointment', '{"appointment_id": "A002", "patient_id": "P001", "doctor_name": "Dr. Moore", "appointment_date": "2023-02-10", "reason": "Follow-up"}');
select documentdb_api.insert_one('documentdb','appointment', '{"appointment_id": "A004", "patient_id": "P003", "doctor_name": "Dr. Smith", "appointment_date": "2024-03-12", "reason": "Allergy consultation"}');
select documentdb_api.insert_one('documentdb','appointment', '{"appointment_id": "A005", "patient_id": "P004", "doctor_name": "Dr. Moore", "appointment_date": "2024-04-15", "reason": "Migraine treatment"}');
select documentdb_api.insert_one('documentdb','appointment', '{"appointment_id": "A007","patient_id": "P001", "doctor_name": "Dr. Milind", "appointment_date": "2024-06-05", "reason": "Blood test"}');
select documentdb_api.insert_one('documentdb','appointment', '{ "appointment_id": "A009", "patient_id": "P003", "doctor_name": "Dr. Smith","appointment_date": "2025-01-20", "reason": "Follow-up visit"}');
```

The example presents each patient along with the doctors visited.

```sql
SELECT cursorpage FROM documentdb_api.aggregate_cursor_first_page('documentdb', '{ "aggregate": "patient", "pipeline": [ { "$lookup": { "from": "appointment","localField": "patient_id", "foreignField": "patient_id", "as": "appointment" } },{"$unwind":"$appointment"},{"$project":{"_id":0,"name":1,"appointment.doctor_name":1,"appointment.appointment_date":1}} ], "cursor": { "batchSize": 3 } }');
```