
all: convert resize

convert:
	mogrify -format png *.webp
	mogrify -format png *.jpg
	
resize: *.png

%.png:
	mogrify -resize 128x -background white -gravity center -extent 128x128 $@
