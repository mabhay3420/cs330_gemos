#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


// utility function for parsing the path
void moveToDirectory(char* path, char* lastName);

// utility function for reading tar file
void readTarFile(char* flag, int tarfd,int listfd,char* singleFile);

// utility function for  preparing tarStructure file
int prepareListStructure(int tarfd);

int main(int argc, char **argv)
{
	// atleast 3 arguments
	if (argc < 3)
	{
		printf("Invalid arguments\n");
		exit(-1);
	}

	char *flag = argv[1];

	// create tar
	if (strcmp(flag, "-c") == 0)
	{

		// exactly 4 arguments required
		if (argc != 4)
		{
			printf("Failed to complete creation operation\n");
			exit(-1);
		}

		char lastDir[32] = ".";
		moveToDirectory(argv[2],lastDir);
		if(chdir(lastDir)<0){
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}

		// underlying fd can be obtained using
		// dirfd, close-on-exec flag is set
		// on this file descriptor
		DIR *sourceDir = opendir(".");
		if (sourceDir == NULL)
		{
			printf("Failed to complete creation operation\n");
			exit(-1);
		}

		// open a new file descriptor for this process
		int tarfd = open(argv[3], O_RDWR | O_CREAT, 0644);
		if (tarfd < 0)
		{
			printf("Failed to complete creation operation\n");
			exit(-1);
		}

		// sorry no function for getting count directly
		int fileCount = 0;

		struct dirent *dirEntry;
		// read directory content
		// offset of fd of sourceDir moves automatically
		while ((dirEntry = readdir(sourceDir)) != NULL)
		{
			// file names are atmax 16 character long
			char fileName[32];
			strncpy(fileName, dirEntry->d_name, 32);

			// printf("fileName: %s\n", fileName);

			// do not recurse on tar file
			if (strcmp(fileName, argv[3]) == 0)
			{
				// move to the next entry
				continue;
			}

			// do not store current and previous directory content
			if ((strcmp(fileName, ".") == 0) || (strcmp(fileName, "..") == 0))
			{
				// move to the next entry
				continue;
			}

			// file descriptor
			int currfd = open(fileName, O_RDONLY);

			char fileSize[32];
			sprintf(fileSize, "%ld", lseek(currfd, 0, SEEK_END));
			lseek(currfd, 0, SEEK_SET);

			// printf("fileSize: %s\n", fileSize);

			// first 32 bytes represent file name
			if (write(tarfd, fileName, 32) <= 0)
			{
				printf("Failed to complete creation operation\n");
				exit(-1);
			}

			// next 32 bytes represent file size
			if (write(tarfd, fileSize, 32) <= 0)
			{
				printf("Failed to complete creation operation\n");
				exit(-1);
			}

			// next fileSize bytes store the file content
			char buff[32];
			// write only the no of bytes read
			int byteRead;
			while ((byteRead = read(currfd, buff, 32)) > 0)
			{
				write(tarfd, buff, byteRead);
			}

			// done for now
			if (close(currfd) < 0)
			{
				printf("Failed to complete creation operation\n");
				exit(-1);
			}

			fileCount += 1;
		}

		// first 32 bytes represent count of files
		char sfileCount[32];
		sprintf(sfileCount, "%d", fileCount);

		// last 32 bits contains count of files
		if (write(tarfd, sfileCount, 32) <= 0)
		{
			printf("Failed to complete creation operation\n");
			exit(-1);
		}

		// done for now
		if (close(tarfd) < 0)
		{
			printf("Failed to complete creation operation\n");
			exit(-1);
		}
	}
	else if (strcmp(flag, "-d") == 0)
	{
		// extract tar
		if (argc != 3)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}

		// change directory

		// relative path by default
		char lastName[32] = ".";
		moveToDirectory(argv[2],lastName);

		// Now in correct directory
		// open tar file
		int tarfd = open(lastName, O_RDONLY);

		if (tarfd < 0)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}

		// prepare the directory name
		char tarDirName[32];
		strcpy(tarDirName, strtok(lastName, "."));
		strcat(tarDirName, "Dump");

		// create dump directory
		if (mkdir(tarDirName, 0777) < 0)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}

		// In the battle
		if (chdir(tarDirName) < 0)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}


		// read tar file
		readTarFile(argv[1],tarfd,-1,NULL);

		// done
	}
	else if (strcmp(flag, "-e") == 0)
	{
		// Single file extraction is a subset of normal extraction
		// We just need to skip other files

		// extract tar
		if (argc != 4)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}

		// relative path by default
		char lastName[32] = ".";
		moveToDirectory(argv[2],lastName);

		// open tar file, now in correct directory
		int tarfd = open(lastName, O_RDONLY);
		if (tarfd < 0)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}

		// prepare the directory name
		char tarDirName[32] = "IndividualDump";

		// create dump directory
		if (mkdir(tarDirName, 0777) < 0)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}

		// In the battle
		if (chdir(tarDirName) < 0)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}

		// read tar file

		readTarFile(argv[1],tarfd,-1,argv[3]);


		// if reached here means file not found
		printf("No such file is present in tar file\n");
		close(tarfd);
		exit(-1);
	}
	else if (strcmp(flag, "-l") == 0)
	{
		// list files
		// Need to move offset carefully, and we are done

		if (argc != 3)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}

		// relative path by default
		char lastName[32] = ".";
		moveToDirectory(argv[2],lastName);

		// open tar file, now in correct directory
		int tarfd = open(lastName, O_RDONLY);

		if (tarfd < 0)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}

		// Prepare for listing
		int listfd = prepareListStructure(tarfd);

		// read file
		readTarFile(argv[1],tarfd,listfd,NULL);

		// done
	}
	else
	{
		printf("Invalid arguments\n");
		exit(-1);
	}

	return 0;
}
void moveToDirectory(char* path, char* lastName){
	// assume the paths are seprated by '/'
	char *str1, *token;
	// absolute path: argv[2] is NON-NULL
	if (path[0] == '/')
	{
		lastName[0] = '/';
	}

	// reach to the end
	for (str1 = path;;str1 = NULL)
	{
		token = strtok(str1, "/");
		if (token == NULL)
		{
			break;
		}
		// move to the next directory
		if (chdir(lastName) < 0)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}
		// moving deeper
		strcpy(lastName, token);
	}

}

void readTarFile(char* flag, int tarfd,int listfd,char* singleFile){
	// get total no of files
	char sfileCount[32];

	// set offset to the last 32 bytes
	if (lseek(tarfd, -32, SEEK_END) < 0)
	{
		printf("Failed to complete extraction operation\n");
		exit(-1);
	}

	// read exactly 32 bytes
	if (read(tarfd, sfileCount, 32) != 32)
	{
		printf("Failed to complete extraction operation\n");
		exit(-1);
	}
	if (lseek(tarfd, 0, SEEK_SET) < 0)
	{
		printf("Failed to complete extraction operation\n");
		exit(-1);
	}

	// no of files to be extracted
	int fileCount = atoi(sfileCount);

	// extract files one by one from tar file
	for (int i = 0; i < fileCount; i++)
	{
		char fileName[32] = {0};
		char sfileSize[32] = {0};

		// read exactly 32 bytes
		if (read(tarfd, fileName, 32) != 32)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}

		// read exactly 32 bytes
		if (read(tarfd, sfileSize, 32) != 32)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}

		// size of file to be created
		unsigned long long fileSize = atol(sfileSize);

		// listing only
		if(strcmp(flag,"-l") == 0){
			// write file name
			if (write(listfd, fileName, strlen(fileName)) != strlen(fileName))
			{
				printf("Failed to complete extraction operation\n");
				exit(-1);
			}

			// write file size
			strcat(sfileSize, "\n");
			if (write(listfd, sfileSize, strlen(sfileSize)) != strlen(sfileSize))
			{
				printf("Failed to complete extraction operation\n");
				exit(-1);
			}

			// Move to the next file
			if (lseek(tarfd, fileSize, SEEK_CUR) < 0)
			{
				printf("Failed to complete extraction operation\n");
				exit(-1);
			}

			// no need to create files
			continue;
		}

		if(strcmp(flag,"-e") == 0){
			// check if this is not the file we are after
			if (strcmp(fileName, singleFile) != 0)
			{
				// move to the next file
				lseek(tarfd, fileSize, SEEK_CUR);
				continue;
			}
		}

		// File to be extracted in this iteration
		int newTar = open(fileName, O_CREAT | O_RDWR, 0644);

		if (newTar < 0)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}
		while (fileSize > 0)
		{
			int remBytes = 32;
			// file size need not to be a multiple of 32
			if (fileSize < 32)
			{
				remBytes = fileSize;
			}
			char buff[remBytes];
			if (read(tarfd, buff, remBytes) < 0)
			{
				printf("Failed to complete extraction operation\n");
				exit(-1);
			}
			if (write(newTar, buff, remBytes) < 0)
			{
				printf("Failed to complete extraction operation\n");
				exit(-1);
			}

			// update file size
			fileSize -= remBytes;
		}

		// file extracted from tar
		if (close(newTar) < 0)
		{
			printf("Failed to complete extraction operation\n");
			exit(-1);
		}

		if(strcmp(flag,"-e") == 0){
			// done with single file
			close(tarfd);
		}
		// offset of tarfd is at starting of next file name
	}
	// file extracted
	close(tarfd);
}

int prepareListStructure(int tarfd){
	// Prepare for listing
	int listfd = open("tarStructure", O_CREAT | O_RDWR, 0644);
	if (listfd < 0)
	{
		printf("Failed to complete extraction operation\n");
		exit(-1);
	}

	unsigned long long tarSize = lseek(tarfd, 0, SEEK_END);
	char starSize[32];
	sprintf(starSize, "%lld\n", tarSize);
	int byteCount = strlen(starSize);

	// write the size of tar file
	if (write(listfd, starSize, byteCount) != byteCount)
	{
		printf("Failed to complete extraction operation");
		exit(-1);
	}

	// get total no of files
	char sfileCount[32];

	// set offset to the last 32 bytes
	if (lseek(tarfd, -32, SEEK_END) < 0)
	{
		printf("Failed to complete extraction operation\n");
		exit(-1);
	}

	// read exactly 32 bytes
	if (read(tarfd, sfileCount, 32) != 32)
	{
		printf("Failed to complete extraction operation\n");
		exit(-1);
	}
	// Move offset to start of file
	if (lseek(tarfd, 0, SEEK_SET) < 0)
	{
		printf("Failed to complete extraction operation\n");
		exit(-1);
	}

	// no of files to be extracted
	int fileCount = atoi(sfileCount);

	// write the no of files in tar archive
	strcat(sfileCount, "\n");
	byteCount = strlen(sfileCount);
	if (write(listfd, sfileCount, byteCount) != byteCount)
	{
		printf("Failed to complete extraction operation\n");
		exit(-1);
	}

	return listfd;
}
