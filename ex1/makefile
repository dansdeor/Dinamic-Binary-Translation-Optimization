pin_dir:= $(wildcard pin*)

all: pin_tool

run: pin_tool
	./$(pin_dir)/pin -t hw1.so -- ./bzip2 -k -f input.txt

pin_tool:
	cd src && make PIN_ROOT=../$(pin_dir) && cd ..
	cp src/obj-intel64/hw1.so ./hw1.so

clean:
	rm -r src/obj-intel64/ && rm hw1.so
