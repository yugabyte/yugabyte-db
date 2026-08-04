Definitions of the terms used in this project
==============================================================================


Two main strategies are used:

* **Dynamic Masking** offers an altered view of the real data without
  modifying it. Some users may only read the masked data, others may access
  the authentic version.

* **Permanent Destruction** is the definitive action of substituting the
  sensitive information with uncorrelated data. Once processed, the authentic
  data cannot be retrieved.

The data can be altered with several techniques:

* **Deletion** or **Nullification** simply removes data.

* **Static Substitution** consistently replaces the data with a generic
   value. For instance: replacing all values of a TEXT column with the value
   "CONFIDENTIAL".

* **Variance** is the action of "shifting" dates and numeric values. For
   example, by applying a +/- 10% variance to a salary column, the dataset will
   remain meaningful.

* **Generalization** reduces the accuracy of the data by replacing it with a
   range of values. Instead of saying "Bob is 28 years old", you can say "Bob
   is between 20 and 30 years old". This is useful for analytics because the
   data remains true.

* **Shuffling** mixes values within the same columns. This method is open to
   being reversed if the shuffling algorithm can be deciphered.

* **Randomization** replaces sensitive data with **random-but-plausible**
   values. The goal is to avoid any identification from the data record while
   remaining suitable for testing, data analysis and data processing.

* **Partial scrambling** is similar to static substitution but leaves out some
   part of the data. For instance : a credit card number can be replaced by
   '40XX XXXX XXXX XX96'

* **Custom rules** are designed to alter data following specific needs. For
   instance, randomizing simultaneously a zipcode and a city name while keeping
   them coherent.

* **Pseudonymization** is a way to **protect** personal information by hiding it
  using additional information. **Encryption** and **Hashing** are two examples
  of pseudonymization techniques. However a pseudonymizated data is still linked
  to the original data.

