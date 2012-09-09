The weekly partitioning scheme uses the ISO week numbering system (http://en.wikipedia.org/wiki/ISO_week_date). This is to keep things consistent since 'week' in date_trunc is used and there is no non-iso option for that. This also avoids any partial weeks during the year.

If turning an existing table with data into a partitioned set, please double check all permissions & constraints after the conversion. Constraints should be good, but permissions are not copied. Indexes are not recreated on the new parent either and should not be.

premake parameter in create parent is how many ADDITIONAL partitions to make outside of the initial, base partition.
If today was Sept 6, 2012, and premake was set to 4, then partitions would be made for the 6th as well as the 7th, 8th, 9th and 10th.
