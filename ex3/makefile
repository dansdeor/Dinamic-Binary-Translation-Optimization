pin_dir:= $(wildcard pin*)

all: pin_tool

run: pin_tool
	./$(pin_dir)/pin -t ex3.so -prof -- ./bzip2 -k -f input.txt
	./$(pin_dir)/pin -t ex3.so -inst -- ./bzip2 -k -f input.txt

pin_tool:
	cd src &&  make PIN_ROOT=../$(pin_dir) obj-intel64/ex3.so && cd ..
	cp src/obj-intel64/ex3.so ./ex3.so

clean:
	rm -r src/obj-intel64/ && rm ex3.so
