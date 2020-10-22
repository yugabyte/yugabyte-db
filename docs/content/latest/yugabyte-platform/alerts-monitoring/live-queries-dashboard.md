---
headerTitle: Live Queries dashboard
linkTitle: Live Queries dashboard
description: Live Queries dashboard
menu:
  latest:
    parent: back-up-restore-universes
    identifier: configure-backup-storage
    weight: 10
isTocNested: true
showAsideToc: true
---

Use the Live Queries dashboard to monitor and display current running queries on your YugabyteDB universes. You can use this data to:
- Visually identify relevant database operations
- Evaluate query execution times
- Discover any potential queries for performance optimization

All user roles — Super Admin, Admin, and Read-only — are granted access to use the Live Queries dashboard.

{{< note title="Note" >}}

There is no significant performance overhead on databases because the queries are fetched on-demand and are not tracked in the background.

{{< /note >}}

## Columns description




Steps



1. Go to the universe details page and click on the Queries tab. Select the API type from the drop-down. **Universe Name > Queries > API type (YSQL)** 



<p id="gdcalert1" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/image1.png). Store image on your image server and adjust path/filename/extension if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert2">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>


![alt_text](images/image1.png "image_tooltip")




2. Changing the dropdown to YCQL will refresh the data & alter the column headers



<p id="gdcalert2" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/image2.png). Store image on your image server and adjust path/filename/extension if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert3">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>


![alt_text](images/image2.png "image_tooltip")




3. Click the search bar and observe the column filter dropdown that appears which allows you to use a query language for filtering data based on certain fields



<p id="gdcalert3" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/image3.png). Store image on your image server and adjust path/filename/extension if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert4">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>


![alt_text](images/image3.png "image_tooltip")




4. You can add multiple search terms that are applied as an intersection. In the below screenshot, adding NODE NAME will filter for all rows with a name containing "puppy-food" and have a "UiniqeSecondaryIndex" somewhere in one of its data cells.



<p id="gdcalert4" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/image4.png). Store image on your image server and adjust path/filename/extension if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert5">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>


![alt_text](images/image4.png "image_tooltip")


The filtering allows for comparisons on numbers columns (Elapsed Time) using >, >=, &lt;, and &lt;= to search for values that are greater than, greater than or equal to, less than, and less than or equal to another value (Elapsed Time:&lt;50).  You can also use the range syntax n..n to search for values within a range, where the first number n is the lowest value and the second is the highest value. The range syntax supports tokens like the following: n..* which is equivalent to >=n. Or *..n which is the same as &lt;=n

 



5. Clicking on one of the rows will cause a sidebar to appear with a full view of the query statement, along with all the column data



<p id="gdcalert5" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/image5.png). Store image on your image server and adjust path/filename/extension if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert6">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>


![alt_text](images/image5.png "image_tooltip")


You will also find additional pre-filtered navigation links from different pages to the Live Query page 



*   From the Metrics tab to the Queries tab when the user selects a node from the dropdown



<p id="gdcalert6" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/image6.png). Store image on your image server and adjust path/filename/extension if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert7">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>


![alt_text](images/image6.png "image_tooltip")




*   The Nodes page to link to the Live Queries page with the specific node pre-filtered.

    

<p id="gdcalert7" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/image7.png). Store image on your image server and adjust path/filename/extension if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert8">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>


![alt_text](images/image7.png "image_tooltip")

