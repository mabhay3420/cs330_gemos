#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>


// TODO: write python test

/**
 * argc: no of arguments in the command line
 * argv: NULL terminated pointer array
 * 	 containing arguments
 */
int main(int argc, char **argv) {

	// one input atleast
	if (argc < 2) {
		printf("UNABLE TO EXECUTE\n");
		exit(-1);
	}

	// last argument before NULL
	unsigned long long result = atol(argv[argc - 1]);

	// operation
	result = round(result * result);

	// conversion to string
	char sresult[32];
	if (sprintf(sresult, "%llu", result) < 0) {
		printf("UNABLE TO EXECUTE\n");
		exit(-1);
	}

	// prepare for child process
	argv[argc - 1] = sresult;

	// process remaining
	if (argc > 2) {

		// A successful overlay destroys the previous memory address space
		// of the process, and all its memory areas, that were not shared
		// are reclaimed by the operating system. Consequently, ** all its
		// data that were not passed to the new program**, or otherwise
		// saved, become lost.

		// argument to child
		char **cargv = argv + 1;
		if (execv(cargv[0], cargv) < 0) {
			printf("UNABLE TO EXECUTE\n");
			exit(-1);
		}
	}

	// done
	printf("%s\n", sresult);
	return 0;
}
