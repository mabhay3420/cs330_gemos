import random
import os

# create a new file


def create_new_file(file_name):
    # create a new file
    file_name = file_name + ".txt"
    file = open(file_name, "w+")
    file.close()
    return file_name


# output file
outFile = create_new_file("output")
inFile = create_new_file("input")
f = open(inFile, "a")
g = open(outFile,"a")

# pipe
input = ""

# process 1 writes 2000 bytes
for i in range(2000):
	input += str(random.randint(0, 9))
f.write(input)
f.write("\n")

# Process 1 reads 1000 bytes
g.write(input[0:1000])
g.write("\n")
input = input[1000:]

# Process 2 write 2496 bytes to input file
s = ""
for i in range(2096):
	input += str(random.randint(0, 9))
	s += input[-1]

s2 = ""
for i in range(400):
	input = str(random.randint(0, 9)) + input
	s2 = input[0] + s2;

f.write(s+s2)
f.write("\n")

# process 1 reads 3296 bytes
g.write(input[400:])
g.write(input[0:200])
g.write("\n")
input = input[200:400]

f.close()
g.close()