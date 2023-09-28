pin_dir := $(wildcard pin-*)

all: run

run: pin_tool
#./$(pin_dir)/pin -t project.so -prof -- ./test.out
#./$(pin_dir)/pin -t project.so -opt -- ./test.out
	./$(pin_dir)/pin -t project.so -prof -- ./bzip2 -k -f input.txt
	./$(pin_dir)/pin -t project.so -opt -- ./bzip2 -k -f input.txt

pin_tool:
#gcc test.c -o test.out
	cd src &&  make PIN_ROOT=../$(pin_dir) obj-intel64/project.so && cd ..
	cp src/obj-intel64/project.so ./project.so

clean:
	rm -r src/obj-intel64/ && rm project.so
