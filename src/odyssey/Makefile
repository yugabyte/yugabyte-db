BUILD_TEST_DIR=build
BUILD_REL_DIR=build
BUILD_TEST_ASAN_DIR=build-asan
ODY_DIR=$(PWD)
TMP_BIN:=$(ODY_DIR)/tmp

FMT_BIN:=clang-format-10
CMAKE_BIN:=cmake

SKIP_CLEANUP_DOCKER:=

CMAKE_FLAGS:=-DCC_FLAGS="-Wextra -Wstrict-aliasing" -DUSE_SCRAM=YES
BUILD_TYPE=Release

DEV_CONF=./config-examples/odyssey-dev.conf
COMPILE_CONCURRENCY=8

.PHONY: clean apply_fmt

clean:
	rm -fr $(TMP_BIN)
	rm -fr $(BUILD_TEST_DIR)
	rm -fr $(BUILD_TEST_ASAN_DIR)

local_build: clean
	$(CMAKE_BIN) -S $(ODY_DIR) -B$(BUILD_TEST_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(CMAKE_FLAGS)
	make -C$(BUILD_TEST_DIR) -j$(COMPILE_CONCURRENCY)

local_run: 
	$(BUILD_TEST_DIR)/sources/odyssey $(DEV_CONF)

console_run: 
	$(BUILD_TEST_DIR)/sources/odyssey $(DEV_CONF) --verbose --console --log_to_stdout

fmtinit:
	git submodule init
	git submodule update

fmt: fmtinit
	run-clang-format/run-clang-format.py -r --clang-format-executable $(FMT_BIN) modules sources stress test third_party

apply_fmt:
	find ./ -maxdepth 5 -iname '*.h' -o -iname '*.c' | xargs $(FMT_BIN) -i

build_asan: clean
	mkdir -p $(BUILD_TEST_ASAN_DIR)
	cd $(BUILD_TEST_ASAN_DIR) && $(CMAKE_BIN) -DCMAKE_BUILD_TYPE=ASAN $(ODY_DIR) && make -j$(COMPILE_CONCURRENCY)

copy_asan_bin:
	cp $(BUILD_TEST_ASAN_DIR)/sources/odyssey ./docker/bin/odyssey-asan

build_release: clean
	mkdir -p $(BUILD_REL_DIR)
	cd $(BUILD_REL_DIR) && $(CMAKE_BIN) -DCMAKE_BUILD_TYPE=Release $(ODY_DIR) $(CMAKE_FLAGS) && make -j$(COMPILE_CONCURRENCY)

copy_release_bin:
	cp $(BUILD_TEST_DIR)/sources/odyssey ./docker/bin/

copy_test_bin:
	cp $(BUILD_TEST_DIR)/test/odyssey_test ./docker/bin/

build_dbg: clean
	mkdir -p $(BUILD_TEST_DIR)
	cd $(BUILD_TEST_DIR) && $(CMAKE_BIN) -DCMAKE_BUILD_TYPE=Debug -DUSE_SCRAM=YES $(ODY_DIR) && make -j$(COMPILE_CONCURRENCY)

gdb: build_dbg
	gdb --args ./build/sources/odyssey $(DEV_CONF)  --verbose --console --log_to_stdout

copy_dbg_bin:
	cp $(BUILD_TEST_DIR)/sources/odyssey ./docker/bin/odyssey-dbg

run_test_prep: build_asan copy_asan_bin build_dbg copy_dbg_bin build_release copy_release_bin copy_test_bin

run_test:
	# change dir, test would not work with absolute path
	./cleanup-docker.sh
	docker-compose -f ./docker-compose-test.yml up --exit-code-from odyssey

submit-cov:
	mkdir cov-build && cd cov-build
	$(COV-BIN-PATH)/cov-build --dir cov-int make -j 4 && tar czvf odyssey.tgz cov-int && curl --form token=$(COV_TOKEN) --form email=$(COV_ISSUER) --form file=@./odyssey.tgz --form version="2" --form description="scalable potgresql connection pooler"  https://scan.coverity.com/builds\?project\=yandex%2Fodyssey


BUILD_VERSION:=
BUILD_NUM:=

build-docker-pkg:
	docker build -f ./docker/dpkg/Dockerfile . --tag odybuild:1.0 && docker run -e VERSION=$(BUILD_VERSION) -e BUILD_NUMBER=$(BUILD_NUM) odybuild:1.0

prefix = /usr/local

install:
	install -D build/sources/odyssey  $(DESTDIR)$(prefix)/bin/odyssey

start-dev-env:
	docker-compose build dev
	docker-compose up -d dev
