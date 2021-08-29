#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv) {
  // atleast 3 arguments
  if (argc < 3) {
    printf("Invalid arguments\n");
    exit(-1);
  }

  char *flag = argv[1];
  // create tar
  if (strcmp(flag, "-c") == 0) {

    // exactly 4 arguments required
    if (argc != 4) {
      printf("Invalid arguments\n");
      exit(-1);
    }

    // move to this directory
    chdir(argv[2]);

    // underlying fd can be obtained using
    // dirfd, close-on-exec flag is set
    // on this file descriptor
    DIR *sourceDir = opendir(".");
    if (sourceDir == NULL) {
      printf("Failed to complete creation operation");
      exit(-1);
    }

    // open a new file descriptor for this process
    int tarfd = open(argv[3], O_RDWR | O_CREAT, 0644);

    // sorry no function for getting count directly
    int fileCount = 0;

    struct dirent *dirEntry = readdir(sourceDir);
    // read directory content
    while (dirEntry != NULL) {

      // file names are atmax 16 character long
      char fileName[32];
      strncpy(fileName, dirEntry->d_name, 32);
      printf("fileName: %s\n", fileName);

      // do not recurse on tar file
      if (strcmp(fileName, argv[3]) == 0) {
        // move to the next entry
        dirEntry = readdir(sourceDir);
        continue;
      }

      // do not store current and previous directory content
      if ((strcmp(fileName, ".") == 0) || (strcmp(fileName, "..") == 0)) {
        // move to the next entry
        dirEntry = readdir(sourceDir);
        continue;
      }

      // file descriptor
      int currfd = open(fileName, O_RDONLY);

      char fileSize[32];
      sprintf(fileSize, "%ld", lseek(currfd, 0, SEEK_END));
      lseek(currfd, 0, SEEK_SET);

      printf("fileSize: %s\n", fileSize);
      // first 32 bytes represent file name
      if (write(tarfd, fileName, 32) <= 0) {
        printf("Failed to complete creation operation");
        exit(-1);
      }

      // next 32 bytes represent file size
      if (write(tarfd, fileSize, 32) <= 0) {
        printf("Failed to complete creation operation");
        exit(-1);
      }

      // TODO: write fileName and size of file
      // seprated by newline and then content of the file

      // next fileSize bytes store the file content
      char buff[32];
      while (read(currfd, buff, 32) > 0) {
        write(tarfd, buff, 32);
      }

      // done for now
      if (close(currfd) < 0) {
        printf("Failed to complete creation operation");
        exit(-1);
      }

      // move to the next entry
      dirEntry = readdir(sourceDir);
      fileCount += 1;
    }

    // first 32 bytes represent count of files
    char sfileCount[32];
    sprintf(sfileCount, "%d", fileCount);

    // last 32 bits contains count of files
    if (write(tarfd, sfileCount, 32) <= 0) {
      printf("Failed to complete creation operation");
      exit(-1);
    }

    // done for now
    if (close(tarfd) < 0) {
      printf("Failed to complete creation operation");
      exit(-1);
    }
  } else if (strcmp(flag, "-d") == 0) {
    // extract tar
  } else if (strcmp(flag, "-e") == 0) {
    // single file extraction
  } else if (strcmp(flag, "-l") == 0) {
    // list files
  } else {
    printf("Invalid arguments\n");
    exit(-1);
  }

  return 0;
}
