Ensure that the node/npm binaries are recent. For example,
\$ npm -v
7.24.0

\$ node -v
v16.10.0

### `npm install`

Install all the dependencies needed for the project

### `npm start`

Runs the app in the development mode.<br>
Open [http://localhost:3000](http://localhost:3000) to view it in the browser.

Ensure that the yugaware side is also running using `sbt run` in ~/code/yugaware directory.<br>

The page will reload if you make edits.<br>
You will also see any lint errors in the console.

### `npm run build`

Builds the app for production to the `build` folder.<br>
It correctly bundles React in production mode and optimizes the build for the best performance.

The build is minified and the filenames include the hashes.<br>
Your app is ready to be deployed!

### Generating Map Tiles and uploading to S3

The maps in Yugaware are generated using TileMill and mbTiles project and uploaded to S3, from where they are downloaded into /public folder during the build process.
To generate your own Map tiles, do the following -

1. Download TileMill or build from source https://tilemill-project.github.io/tilemill/
2. Create Outline World Map , customize styles using CartoCSS http://tilemill-project.github.io/tilemill/docs/manual/carto/
3. Export to .mbTiles file (choose zoom level, center, tile quality etc. all of which will affect the size of your output)
4. Use mbutil to generate base map pngs.
5. git clone git://github.com/mapbox/mbutil.git
6. mb-util <our-filename-here>.mbtiles <destination-folder-name>
7. Upload to S3 bucket, make sure permissions are open, set content/type to "image/png"
