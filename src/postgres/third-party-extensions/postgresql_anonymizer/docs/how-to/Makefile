#
#
#

BUILDIR?=_build

##
## Pandoc
##
PANDOC?=pandoc
SLIDES_THEME?=white

# if pandoc is not installed, let's use pandocker
ifeq (, $(shell which $(PANDOC)))
	PANDOCKER_TAG?=latest
endif

ifneq ($(PANDOCKER_TAG),)
	PANDOC:=docker run --rm -it --privileged --volume $(CURDIR):/pandoc dalibo/pandocker:$(PANDOCKER_TAG)
endif

PANDOC_ARGS?=--standalone --strip-comments
PANDOC+= $(PANDOC_ARGS)

##
## Source and Objects
##
SOURCE_FILES ?= $(sort $(wildcard *.md)) # the order is important
SOURCE_FILES := $(filter-out README.md, $(SOURCE_FILES))

MD_FILES = $(patsubst %.md,$(BUILDIR)/%.md, $(SOURCE_FILES))
SLIDES_FILES = $(MD_FILES:.md=.slides.html)
HANDOUT_FILES = $(MD_FILES:.md=.handout.pdf)
HANDOUT2_PDF_FILES = $(MD_FILES:.md=.handout2.pdf)
HANDOUT_HTML_FILES = $(MD_FILES:.md=.handout.html)

HOW-TO_MD = $(BUILDIR)/how-to.md
HOW-TO_HANDOUT_PDF = $(HOW-TO_MD:.md=.handout.pdf)
HOW-TO_HANDOUT_HTML = $(HOW-TO_MD:.md=.handout.html)
HOW-TO_HANDOUT_EPUB = $(HOW-TO_MD:.md=.handout.epub)
HOW-TO_SLIDES_HTML = $(HOW-TO_MD:.md=.slides.html)

.PRECIOUS: $(MD_FILES)

DEBUG ?= 0
ifeq ($(DEBUG), 1)
.PRECIOUS: $(HOW-TO_MD) $(MD_FILES)
else
.INTERMEDIATE: $(MD_FILES)
endif

all: how-to


##
## Build
##


how-to: $(HOW-TO_HANDOUT_HTML) $(HOW-TO_HANDOUT_PDF) $(HOW-TO_SLIDES_HTML) #: build the main workshop exports

markdown: $(MD_FILES) #: build all intermediate markdown files
slides: $(SLIDES_FILES) #: build all presentations
handout: $(HANDOUT_FILES) #: build all handouts in PDF
handout_html: $(HANDOUT_HTML_FILES) #: build all intermediate HTML files

$(BUILDIR):
	mkdir -p $(BUILDIR)

##
## First generate an intermediate markdown file
## We'll execute all SQL queries inside the SOURCE_FILES
##
$(HOW-TO_MD): $(SOURCE_FILES) | $(BUILDIR)
	$(PANDOC) $(SOURCE_FILES) \
		--to markdown \
		--metadata-file=pandoc-run-postgres.yaml \
		--filter=pandoc-run-postgres \
		--output=$@


##
## Output Formats
##
%.slides.html: %.md
	$(PANDOC) $^ \
		--to revealjs \
		--slide-level=3 \
		--variable theme=$(SLIDES_THEME) \
		--embed-resources \
		--output $@

%.handout.epub: %.md
	$(PANDOC) $^ --output $@

%.handout.html: %.md
	$(PANDOC) $^ \
		--template=uikit \
		--embed-resources \
		--toc \
		--output $@

%.handout.pdf: %.md
	$(PANDOC)  $^ \
		--metadata-file=metadata.yaml \
		--filter pandoc-latex-environment \
		--top-level-division=chapter \
		--pdf-engine=xelatex \
		--listings \
		--highlight-style tango \
		--template=eisvogel \
		--output $@

# used for debugging
%.handout.tex: %.md
	$(PANDOC)  $^ \
		--metadata-file=metadata.yaml \
		--filter pandoc-latex-environment \
		--top-level-division=chapter \
		--pdf-engine=xelatex \
		--listings \
		--highlight-style tango \
		--template=eisvogel \
		--output $@

clean: #: remove all built objects
	rm -fr $(BUILDIR)

##
## H E L P
##

default:: help

help::  #: display this message.
	@echo
	@echo "Available targets:"
	@echo
	@gawk 'match($$0, /([^:]*):.+#'': (.*)/, m) { printf "    %-16s%s\n", m[1], m[2]}' $(MAKEFILE_LIST) | sort
	@echo

