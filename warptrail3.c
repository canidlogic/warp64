/*
 * warptrail3.c
 * ============
 * 
 * Report the hex codes for the last three bytes of a file as well as
 * the byte offset of the third from last byte.
 * 
 * Syntax:
 * 
 *   ./warptrail3 input.binary
 * 
 * This utility is meant for use during the key recovery procedure.  It
 * won't directly recover the scrambling key of a scrambled file, but it
 * will give you the information you need to do that.
 * 
 * Must compile with _FILE_OFFSET_BITS=64
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* POSIX headers */
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * Check 64-bit file mode
 * ======================
 */

#ifdef _FILE_OFFSET_BITS
#if (_FILE_OFFSET_BITS != 64)
#error Need to define _FILE_OFFSET_BITS=64
#endif
#else
#error Need to define _FILE_OFFSET_BITS=64
#endif

/*
 * Program entrypoint
 * ==================
 */

int main(int argc, char *argv[]) {
  
  int status = 1;
  int i = 0;
  const char *pModule = NULL;
  
  int64_t fl = 0;
  uint8_t trail[3];
  
  const char *pPath = NULL;
  struct stat st;
  int fh = -1;
  
  /* Initialize structures and arrays */
  memset(&st, 0, sizeof(struct stat));
  memset(trail, 0, 3);
  
  /* Get the module name */
  pModule = NULL;
  if ((argc > 0) && (argv != NULL)) {
    pModule = argv[0];
  }
  if (pModule == NULL) {
    pModule = "warptrail3";
  }
  
  /* If no parameters provided, print help screen and fail */
  if (argc <= 1) {
    status = 0;
    fprintf(stderr, "Warp64 trailer examination\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Syntax:\n");
    fprintf(stderr, "  warptrail3 [input_path]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "[input_path] is path to input file\n");
  }
  
  /* Must be exactly one parameter beyond module name */
  if (status && (argc != 2)) {
    status = 0;
    fprintf(stderr, "%s: Wrong number of parameters!\n", pModule);
  }
  
  /* Check that parameters are present */
  if (status) {
    if (argv == NULL) {
      abort();
    }
    for(i = 0; i < argc; i++) {
      if (argv[i] == NULL) {
        abort();
      }
    }
  }
  
  /* Get the path to the input file */
  if (status) {
    pPath = argv[1];
  }
  
  /* Make sure the file path is for an existing regular file */
  if (status) {
    if (stat(pPath, &st)) {
      status = 0;
      fprintf(stderr, "%s: Failed to stat '%s'\n",
              pModule, pPath);
    }
  }
  if (status) {
    if (!(S_ISREG(st.st_mode))) {
      status = 0;
      fprintf(stderr, "%s: '%s' is not a regular file!\n",
              pModule, pPath);
    }
  }
  
  /* Open the file for reading */
  if (status) {
    fh = open(pPath, O_RDONLY);
    if (fh < 0) {
      status = 0;
      fprintf(stderr, "%s: Failed to open '%s'\n", pModule, pPath);
    }
  }
  
  /* Get the length of the input file */
  if (status) {
    fl = (int64_t) lseek(fh, 0, SEEK_END);
    if (fl < 0) {
      status = 0;
      fprintf(stderr, "%s: Failed to get length of '%s'!\n",
              pModule, pPath);
    }
  }
  
  /* Make sure input file length is at least three */
  if (status && (fl < 3)) {
    status = 0;
    fprintf(stderr, "%s: '%s' is less than three bytes long!\n",
            pModule, pPath);
  }
  
  /* Seek to three bytes from the end and store the byte offset here */
  if (status) {
    fl = (int64_t) lseek(fh, -3, SEEK_END);
    if (fl < 0) {
      status = 0;
      fprintf(stderr, "%s: Failed to seek in '%s'!\n", pModule, pPath);
    }
  }
  
  /* Read the last three bytes into trail */
  if (status) {
    if (read(fh, trail, 3) != 3) {
      status = 0;
      fprintf(stderr, "%s: Failed to read from '%s'!\n",
        pModule, pPath);
    }
  }
  
  /* Close file handle if opened */
  if (fh >= 0) {
    if (close(fh)) {
      fprintf(stderr, "%s: Failed to close file!\n", pModule);
    }
    fh = -1;
  }
  
  /* Report results */
  if (status) {
    printf("Byte offset %lld decimal:\n", (long long) fl);
    printf("0x%02x 0x%02x 0x%02x\n",
              (int) trail[0],
              (int) trail[1],
              (int) trail[2]);
  }
  
  /* Invert status and return */
  if (status) {
    status = 0;
  } else {
    status = 1;
  }
  return status;
}
