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

# process i writes 1000 bytes
for i in range(1000):
	input += str(random.randint(0, 9))
f.write(input)
f.write("\n")

# process j reads 500 bytes
g.write(input[0:500])
g.write("\n")

# process k reads 200 bytes
g.write(input[0:200])
g.write("\n")


# process k writes 1000 bytes

s = ""
for i in range(1000):
	input += str(random.randint(0, 9))
	s += input[-1]

f.write(s)
f.write("\n")

# some process calls flush
# nothing happens
#process i closes its read end
# some process calls flush
input = input[200:]

# process k calls read 3000 bytes
# but only 1800 bytes will be read
g.write(input)
g.write("\n")

# process j writes 2196 bytes
s = ""
for i in range(2096):
	input += str(random.randint(0, 9))
	s += input[-1]
s2 = ""
for i in range(100):
	input = str(random.randint(0, 9)) + input
	s2 = input[0] + s2

f.write(s+s2)
f.write("\n")

# process k reads 2196 bytes
g.write(input[1900:])
g.write(input[0:100])
g.write("\n")
g.write()

# proces j tries to write 200 bytes
s = ""
for i in range(100):
	s += str(random.randint(0,9))

input = input[0:100] + s + input[100:]

f.close()
g.close()