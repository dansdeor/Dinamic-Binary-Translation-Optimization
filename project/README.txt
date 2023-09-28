Name: Dan Sdeor ID:209509181 Email:dansdeor@campus.technion.ac.il
Name: On Fabian ID:308141340 Email:onfabian@campus.technion.ac.il

1. place the pin-3.25 folder, and the executables (bzip2 executable and input.txt for example) under project folder
2. run the command "make all" under project folder to compile the pintool and execute the code.
You can also run "make pin_tool" to only compile and manually run the commands for profiling and optimization after you compile

for example:
./pin-3.25-98650-g8f6168173-gcc-linux/pin -t project.so -prof -- ./bzip2 -k -f input.txt
./pin-3.25-98650-g8f6168173-gcc-linux/pin -t project.so -inst -- ./bzip2 -k -f input.txt

make sure you execute the commands in this order if you want to run manually!

csv row:
each row explains what optimizations to run on the specific specified routine
routine name , routine address , heat_score of the routine , optimization mode , reorder branch offset from the start of the routine,call to be inlined by callee routine offset from the start of the routine , inline callee name

for example:
main,0x55a886cc61a2,17,3,23,96,foo

we use multiple criteria to approve the inlining of a function such as:
Last instruction is not ret
More than 1 ret instructions
Checks for indirect branches in the routine
Checks for jumps outside the routine
Check that RSP has no negative displacement and RBP has no positive displacement
Multiple calls instructions

for the branch we use for reorder the not taken and taken block we check that:
 conditional jump is taken / number of times we hit that branch  >= BRANCH_THRESHOLD=80%

 than we pick the most hot conditional branch

 to check the hotness of the routine we use the number of executed instructions as parameter
 to check the hotness of the branch we use the number of branch executions as parameter
