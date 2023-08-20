Name: Dan Sdeor ID:209509181 Email:dansdeor@campus.technion.ac.il
Name: ID: Email:

1. place the pin-3.25 folder, bzip2 executable and input.txt under ex3 folder
2. run the command "make run" under ex3 folder to compile the pintool and execute the code.
You can also run "make" to only compile and manually run the commands:

./pin-3.25-98650-g8f6168173-gcc-linux/pin -t ex3.so -prof -- ./bzip2 -k -f input.txt
./pin-3.25-98650-g8f6168173-gcc-linux/pin -t ex3.so -inst -- ./bzip2 -k -f input.txt

make sure you execute the commands in this order if you want to run manually!