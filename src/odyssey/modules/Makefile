PATH_SO=/tmp/od_modules/audit_module.so
SO_DIR=/tmp/od_modules

init:
	mkdir -p ${SO_DIR}


compile_all: init
	gcc  -shared -o ${PATH_SO} -I../sources -I../third_party/machinarium/sources -I../third_party/kiwi/ audit_module.c
