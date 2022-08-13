/*
 * warp64.c
 * ========
 * 
 * C implementation of Warp64 scrambling and descrambling.
 * 
 * Syntax:
 * 
 *   ./warp64 -s input.binary
 *   ./warp64 -d input.binary.warp64
 * 
 * -s is scrambling mode.  The scrambled file will be written to a path
 * that is the same as the input path, except with ".warp64" suffixed.
 * The output path is overwritten if it already exists, and if the
 * operation is successful, the original file is deleted at the end.
 * 
 * -d is descrambling mode.  The input file path must end with
 * ".warp64".  The output path is the same as the input path, except
 * that ".warp64" is dropped from the end of it.  The output path is NOT
 * overwritten if it already exists, and the scrambled file is NOT
 * deleted.
 * 
 * The scrambling key will be requested and then read from the console,
 * so that it is not stored in the console history.
 * 
 * Must compile with _FILE_OFFSET_BITS=64
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* POSIX headers */
#include <fcntl.h>
#include <sys/stat.h>
#include <termios.h>
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
 * Constants
 * =========
 */

/*
 * The maximum length of the scrambling key that can be read.
 */
#define MAX_KEY_LENGTH (255)

/*
 * The suffix used for scrambled files.
 */
#define FILE_SUFFIX ".warp64"

/*
 * The target number of bytes for the memory-mapped window.
 */
#define WINDOW_TARGET (4194304L)

/*
 * Data types
 * ==========
 */

/*
 * Stores a null-terminated scrambling key.
 */
typedef struct {
  char kbuf[MAX_KEY_LENGTH + 1];
} KEY_BUFFER;

/*
 * Local data
 * ==========
 */

/*
 * The name of the executable module, for use in diagnostic messages.
 * 
 * This is set at the start of the entrypoint.
 */
static const char *pModule = NULL;

/*
 * The window size to use for memory mapping.
 * 
 * Set near the start of the entrypoint.
 */
static size_t m_winsize = 0;

/*
 * Local functions
 * ===============
 */

/* Prototypes */
static int readKey(KEY_BUFFER *kb);
static int decode64(int c);
static int32_t deriveKey(const char *pKey);
static int warp64(
    const char * pInputPath,
    const char * pOutputPath,
          int    descramble,
    const char * pKey);

/*
 * Read the scrambling key from standard input, suppressing echo so that
 * the key is not displayed.
 * 
 * Error messages are printed if failure.  This function will also check
 * that each character read decodes with decode64().
 * 
 * Parameters:
 * 
 *   kb - the buffer that will hold the key
 * 
 * Return:
 * 
 *   non-zero if successful, zero if error
 */
static int readKey(KEY_BUFFER *kb) {
  int status = 1;
  int console_changed = 0;
  int chars_read = 0;
  int c = 0;
  int i = 0;
  struct termios old_attr;
  struct termios new_attr;
  
  /* Initialize local structures */
  memset(&old_attr, 0, sizeof(struct termios));
  memset(&new_attr, 0, sizeof(struct termios));
  
  /* Check parameter */
  if (kb == NULL) {
    abort();
  }
  
  /* Clear return structure */
  memset(kb, 0, sizeof(KEY_BUFFER));
  
  /* Get the input attributes */
  if (tcgetattr(STDIN_FILENO, &old_attr)) {
    status = 0;
    fprintf(stderr, "%s: Failed to get console input attributes!\n",
              pModule);
    fprintf(stderr, "%s: Make sure input is not redirected.\n",
              pModule);
  }
  
  /* Copy input attributes to new attributes and disable local mode ECHO
   * in new attributes to stop character echo */
  if (status) {
    memcpy(&new_attr, &old_attr, sizeof(struct termios));
    if (new_attr.c_lflag & ECHO) {
      new_attr.c_lflag ^= ECHO;
    }
  }
  
  /* Update console input immediately to disable echo and set the
   * console_changed flag */
  if (status) {
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_attr)) {
      status = 0;
      fprintf(stderr, "%s: Failed to update console attributes!\n",
                pModule);
      fprintf(stderr, "%s: Make sure input is not redirected.\n",
              pModule);
    } else {
      console_changed = 1;
    }
  }
  
  /* Read characters from console */
  while (status) {
    /* Read a character */
    c = getchar();
    
    /* If we got EOF, handle that */
    if (c == EOF) {
      if (feof(stdin)) {
        /* We reached end of input, so we can leave the loop */
        break;
        
      } else {
        /* There was an input error */
        status = 0;
        fprintf(stderr, "%s: I/O error reading key!\n", pModule);
      }
    }
  
    /* If we got line break, leave the loop */
    if (status && (c == '\n')) {
      break;
    }
  
    /* If we exceeded the buffer, silently set characters read to -1 */
    if (status && (chars_read >= MAX_KEY_LENGTH)) {
      chars_read = -1;
    }
    
    /* If current character is nul or greater than 128, set it to 128
     * (which is also an invalid character) */
    if (status && ((c == 0) || (c > 128))) {
      c = 128;
    }
    
    /* Provided that buffer hasn't overflown, add current character to
     * buffer and increment chars_read */
    if (status && (chars_read >= 0)) {
      (kb->kbuf)[chars_read] = (char) c;
      chars_read++;
    }
  }
  
  /* If we didn't read any characters or we read too many, then report
   * errors */
  if (status && (chars_read == 0)) {
    status = 0;
    fprintf(stderr, "%s: Key may not be empty!\n", pModule);
    
  } else if (status && (chars_read < 0)) {
    status = 0;
    fprintf(stderr, "%s: Key may have at most %d characters!\n",
                pModule, MAX_KEY_LENGTH);
  }
  
  /* Check that each character is a base-64 character */
  if (status) {
    for(i = 0; i < chars_read; i++) {
      if (decode64((kb->kbuf)[i]) < 0) {
        status = 0;
        fprintf(stderr, "%s: Key may only include A-Za-z0-9+/\n",
                  pModule);
        break;
      }
    }
  }
  
  /* If console input attributes were changed, change them back */
  if (console_changed) {
    if (tcsetattr(STDIN_FILENO, TCSANOW, &old_attr)) {
      status = 0;
      fprintf(stderr, "%s: Failed to reset console input attributes!\n",
              pModule);
    } else {
      console_changed = 0;
    }
  }
  
  /* If failure, reset return buffer */
  if (!status) {
    memset(kb, 0, sizeof(KEY_BUFFER));
  }
  
  /* Return status */
  return status;
}

/*
 * Given a character code c, return the decoded base-64 value.
 * 
 * Parameters:
 * 
 *   c - the character code
 * 
 * Return:
 * 
 *   the base-64 value, or -1 if the given character code wasn't for a
 *   base-64 digit
 */
static int decode64(int c) {
  int result = 0;
  
  if ((c >= 'A') && (c <= 'Z')) {
    result = c - 'A';
    
  } else if ((c >= 'a') && (c <= 'z')) {
    result = (c - 'a') + 26;
    
  } else if ((c >= '0') && (c <= '9')) {
    result = (c - '0') + 52;
    
  } else if (c == '+') {
    result = 62;
    
  } else if (c == '/') {
    result = 63;
    
  } else {
    result = -1;
  }
  
  return result;
}

/*
 * Given a scrambling key of one or more base-64 characters, derive the
 * normalized scrambling key and return it.
 * 
 * The three octets of the normalized scrambling key are stored in the
 * 24 least significant bits of the returned integer value, with the
 * third octet being the least significant eight bits of the integer
 * value.
 * 
 * Error messages are printed if necessary.
 * 
 * Parameters:
 * 
 *   pKey - the base-64 characters for the key
 * 
 * Return:
 * 
 *   the normalized key octets, or -1 if error
 */
static int32_t deriveKey(const char *pKey) {
  int status = 1;
  size_t slen = 0;
  int32_t mixed = 0;
  int32_t acc = 0;
  int d = 0;
  int i = 0;
  char ext[3];
  char b64[4];
  uint8_t cbc[3];
  
  /* Initialize arrays */
  memset(ext, 0, 3);
  memset(b64, 0, 4);
  memset(cbc, 0, 3);
  
  /* Check parameter */
  if (pKey == NULL) {
    abort();
  }
  
  /* Determine length of original key string, excluding terminating
   * nul */
  slen = strlen(pKey);
  
  /* Fail if empty string */
  if (slen < 1) {
    status = 0;
    fprintf(stderr, "%s: Scrambling key may not be empty!\n", pModule);
  }
  
  /* Determine the three extension characters that will be used if
   * padding is necessary */
  if (status) {
    if (slen == 1) {
      ext[0] = pKey[0];
      ext[1] = pKey[0];
      ext[2] = pKey[0];
    
    } else if (slen == 2) {
      ext[0] = pKey[0];
      ext[1] = pKey[1];
      ext[2] = pKey[0];
      
    } else if (slen >= 3) {
      ext[0] = pKey[0];
      ext[1] = pKey[1];
      ext[2] = pKey[2];
      
    } else {
      abort();
    }
  }
  
  /* Keep updating the result until we have processed all of the key */
  if (status) {
    mixed = 0;
    while (slen > 0) {
      /* Fill b64 buffer with four base-64 characters and update slen as
       * well as pKey, using extension characters if necessary */
      if (slen == 1) {
        /* We have one more character, so use that and three
         * extensions */
        b64[0] = pKey[0];
        b64[1] = ext[0];
        b64[2] = ext[1];
        b64[3] = ext[2];
        slen--;
        pKey++;
        
      } else if (slen == 2) {
        /* We have two more characters, so use those and two
         * extensions */
        memcpy(b64, pKey, 2);
        b64[2] = ext[0];
        b64[3] = ext[1];
        slen -= 2;
        pKey += 2;
        
      } else if (slen == 3) {
        /* We have three more characters, so use those and one
         * extension */
        memcpy(b64, pKey, 3);
        b64[3] = ext[0];
        slen -= 3;
        pKey += 3;
        
      } else if (slen >= 4) {
        /* We have at least four more characters, so just use those */
        memcpy(b64, pKey, 4);
        slen -= 4;
        pKey += 4;
        
      } else {
        abort();
      }
      
      /* Decode the four base-64 characters into a packed integer */
      acc = 0;
      for(i = 0; i < 4; i++) {
        d = decode64(b64[i]);
        if (d < 0) {
          status = 0;
          fprintf(stderr, "%s: Scrambling key has bad characters!\n",
                  pModule);
          break;
        }
        acc = (acc << 6) | ((int32_t) d);
      }
      if (!status) {
        break;
      }
      
      /* XOR in the new segment into the mixed key */
      mixed = mixed ^ acc;
    }
  }
  
  /* Unpack the mixed key into the cbc buffer */
  if (status) {
    cbc[0] = (uint8_t) ((mixed >> 16) & 0xff);
    cbc[1] = (uint8_t) ((mixed >>  8) & 0xff);
    cbc[2] = (uint8_t) ( mixed        & 0xff);
  }
  
  /* Perform replacements to make sure no zero components */
  if (status) {
    if (cbc[0] == 0) {
      cbc[0] = (uint8_t) 1;
    }
    if (cbc[1] == 0) {
      cbc[1] = (uint8_t) 2;
    }
    if (cbc[2] == 0) {
      cbc[2] = (uint8_t) 4;
    }
  }
  
  /* Repack the mixed key with replacements */
  if (status) {
    mixed =   (((int32_t) cbc[0]) << 16)
            + (((int32_t) cbc[1]) <<  8)
            + ( (int32_t) cbc[2]       );
  }
  
  /* If failure, set result to -1 */
  if (!status) {
    mixed = -1;
  }
  
  /* Return result */
  return mixed;
}

/*
 * Perform the main program given all the necessary parameters.
 * 
 * Parameters:
 * 
 *   pInputPath - path to the input file
 * 
 *   pOutputPath - path to the output file
 * 
 *   descramble - non-zero if descrambling, zero if scrambling
 * 
 *   pKey - the scrambling key
 * 
 * Return:
 * 
 *   non-zero if successful, zero if error
 */
static int warp64(
    const char * pInputPath,
    const char * pOutputPath,
          int    descramble,
    const char * pKey) {
  
  int status = 1;
  int i;
  
  int32_t cks = 0;
  int32_t key = 0;
  int64_t tval = 0;
  
  int64_t ilen = 0;
  int64_t olen = 0;
  int64_t ctlen = 0;
  
  uint8_t head[8];
  
  int new_file = 0;
  int fIn = -1;
  int fOut = -1;
  
  /* Initialize structures */
  memset(head, 0, 8);
  
  /* Check parameters */
  if ((pInputPath == NULL) || (pOutputPath == NULL) || (pKey == NULL)) {
    abort();
  }
  
  /* Derive the normalized key */
  key = deriveKey(pKey);
  if (key < 0) {
    status = 0;
  }
  
  /* If this is scrambling mode, define the header */
  if (status && (!descramble)) {
    /* Get the current timestamp */
    if (status) {
      tval = (int64_t) time(NULL);
      if (tval < 0) {
        status = 0;
        fprintf(stderr, "%s: Failed to read current time!\n", pModule);
      }
    }
    
    /* Only deal with the 32 least significant bits of the timestamp, so
     * that the first three bytes of the header remain zero */
    if (status) {
      tval = tval & INT64_C(0xffffffff);
    }
    
    /* Unpack the timestamp into the head array in big-endian order,
     * except rotate left once such that the first byte is the second
     * most significant, the last byte is the most significant, and the
     * second from last byte is the least significant */
    if (status) {
      head[7] = (uint8_t) ((tval >> 56) & 0xff);
      head[0] = (uint8_t) ((tval >> 48) & 0xff);
      head[1] = (uint8_t) ((tval >> 40) & 0xff);
      head[2] = (uint8_t) ((tval >> 32) & 0xff);
      head[3] = (uint8_t) ((tval >> 24) & 0xff);
      head[4] = (uint8_t) ((tval >> 16) & 0xff);
      head[5] = (uint8_t) ((tval >>  8) & 0xff);
      head[6] = (uint8_t) ( tval        & 0xff);
    }
    
    /* Get the sum of the first seven bytes of the header */
    if (status) {
      cks = 0;
      for(i = 0; i < 7; i++) {
        cks = cks + ((int32_t) head[i]);
      }
    }
    
    /* Take the sum MOD 256, and then determine what must be added to
     * make the MOD 256 sum zero */
    if (status) {
      cks = cks % 256;
      if (cks != 0) {
        cks = 256 - cks;
      }
    }
    
    /* Overwrite the last header byte (which should normally be zero
     * because it's the most significant byte of the time) with the
     * checksum byte so that the sum of all header bytes MOD 256 is
     * zero */
    if (status) {
      head[7] = (uint8_t) cks;
    }
  }
  
  /* Open the input file for reading */
  if (status) {
    fIn = open(pInputPath, O_RDONLY);
    if (fIn < 0) {
      status = 0;
      fprintf(stderr, "%s: Failed to open '%s'\n", pModule, pInputPath);
    }
  }
  
  /* Open the output file for writing; in descrambling mode, do not
   * allow existing files to be overwritten, but overwrite is OK in
   * scrambling mode; set new_file flag if successfully created a new
   * file */
  if (status) {
    if (descramble) {
      fOut = open(pOutputPath, O_RDWR | O_CREAT | O_EXCL,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
      if (fOut < 0) {
        status = 0;
        fprintf(stderr, "%s: Failed to open '%s'!\n",
                pModule, pOutputPath);
        fprintf(stderr, "%s: Check that '%s' does not exist.\n",
                pModule, pOutputPath);
      }
      
    } else {
      fOut = open(pOutputPath, O_RDWR | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
      if (fOut < 0) {
        status = 0;
        fprintf(stderr, "%s: Failed to open '%s'!\n",
                pModule, pOutputPath);
      }
    }
    if (status) {
      new_file = 1;
    }
  }
  
  /* Get the length of the input file */
  if (status) {
    ilen = (int64_t) lseek(fIn, 0, SEEK_END);
    if (ilen < 0) {
      status = 0;
      fprintf(stderr, "%s: Failed to measure '%s'!\n",
              pModule, pInputPath);
    }
  }
  if (status) {
    if (lseek(fIn, 0, SEEK_SET) != 0) {
      status = 0;
      fprintf(stderr, "%s: Failed to rewind '%s'!\n",
              pModule, pInputPath);
    }
  }
  
  /* If this is descrambling mode, then input file length must be at
   * least eight and then content length is eight less than input file;
   * else, content length is equal to input file */
  if (status && descramble) {
    if (ilen < 8) {
      status = 0;
      fprintf(stderr, "%s: Missing header in '%s'!\n",
              pModule, pInputPath);
    }
    if (status) {
      ctlen = ilen - 8;
    }
  
  } else if (status && (!descramble)) {
    ctlen = ilen;
  
  } else if (status) {
    abort();
  }
  
  /* If this is descrambling mode, then read the last eight bytes and
   * verify the descrambled header */
  if (status && descramble) {
    /* Read the last eight bytes into head */
    if (lseek(fIn, -8, SEEK_END) < 0) {
      status = 0;
      fprintf(stderr, "%s: Failed to read trailer!\n", pModule);
    }
    
    if (status) {
      if (read(fIn, head, 8) != 8) {
        status = 0;
        fprintf(stderr, "%s: Failed to read trailer!\n", pModule);
      }
    }
    
    if (status) {
      if (lseek(fIn, 0, SEEK_SET) != 0) {
        status = 0;
        fprintf(stderr, "%s: Failed to read trailer!\n", pModule);
      }
    }
    
    /* Figure out the index of the scrambling key to use for the first
     * byte of the head */
    if (status) {
      i = (int) ((olen - 8) % 3);
    }
  }
  
  
  /* Compute length of output file based on content length */
  if (status && descramble) {
    olen = ctlen;
    
  } else if (status && (!descramble)) {
    olen = ctlen + 8;
    
  } else if (status) {
    abort();
  }
  
  /* Only process further if output length is non-zero */
  if (status && (olen > 0)) {
  
    /* Expand the output file to the proper length */
    if (status && (olen > 1)) {
      if (lseek(fOut, (off_t) (olen - 1), SEEK_SET) != olen - 1) {
        status = 0;
        fprintf(stderr, "%s: Failed to set output length!\n", pModule);
      }
    }
    if (status) {
      if (write(fOut, &(head[0]), 1) != 1) {
        status = 0;
        fprintf(stderr, "%s: Failed to set output length!\n", pModule);
      }
    }
    if (status) {
      if (lseek(fOut, 0, SEEK_SET) != 0) {
        status = 0;
        fprintf(stderr, "%s: Failed to set output length!\n", pModule);
      }
    }
  
  
  
    /* @@TODO: */
  }
  
  /* Close open file handles */
  if (fIn >= 0) {
    if (close(fIn)) {
      fprintf(stderr, "%s: Failed to close input file!\n", pModule);
    }
    fIn = -1;
  }
  if (fOut >= 0) {
    if (close(fOut)) {
      fprintf(stderr, "%s: Failed to close output file!\n", pModule);
    }
    fOut = -1;
  }
  
  /* If there was a failure and new_file flag is set, remove the output
   * file */
  if ((!status) && new_file) {
    if (unlink(pOutputPath)) {
      fprintf(stderr, "%s: Failed to clean up output file!\n", pModule);
    }
  }
  
  /* @@TODO: */
}

/*
 * Program entrypoint
 * ==================
 */

int main(int argc, char *argv[]) {
  int status = 1;
  
  int i = 0;
  size_t slen = 0;
  size_t suflen = 0;
  long wval = 0;
  long wsz = 0;
  
  int input_suffixed = 0;
  int descramble = 0;
  const char *pInputPath = NULL;
  char *pOutputPath = NULL;
  
  KEY_BUFFER kb;
  struct stat st;
  
  /* Initialize structures */
  memset(&kb, 0, sizeof(KEY_BUFFER));
  memset(&st, 0, sizeof(struct stat));
  
  /* Get the module name */
  pModule = NULL;
  if ((argc > 0) && (argv != NULL)) {
    pModule = argv[0];
  }
  if (pModule == NULL) {
    pModule = "warp64";
  }
  
  /* Figure out the system page size */
  wval = sysconf(_SC_PAGE_SIZE);
  if (wval < 1) {
    fprintf(stderr, "%s: Failed to determine system page size!\n");
    abort();
  }
  
  /* Figure out how many pages in desired window target */
  wsz = WINDOW_TARGET / wval;
  if ((WINDOW_TARGET % wval) != 0) {
    wsz++;
  }
  if (wsz < 1) {
    wsz = 1;
  }
  
  /* Store the computed window size */
  m_winsize = (size_t) (wsz * wval);
  
  /* If no parameters provided, print help screen and fail */
  if (argc <= 1) {
    status = 0;
    fprintf(stderr, "Warp64 binary scrambling and descrambling\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Syntax:\n");
    fprintf(stderr, "  warp64 -s [input_path]\n");
    fprintf(stderr, "  warp64 -d [input_path]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "[input_path] is path to input file\n");
    fprintf(stderr, "-s scrambles input file\n");
    fprintf(stderr, "-d descrambles input file\n");
    fprintf(stderr, "Scrambled files have .warp64 suffix\n");
  }
  
  /* Must be exactly two parameters beyond module name */
  if (status && (argc != 3)) {
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
  
  /* Determine whether scrambling or descrambling */
  if (status) {
    if (strcmp(argv[1], "-s") == 0) {
      descramble = 0;
      
    } else if (strcmp(argv[1], "-d") == 0) {
      descramble = 1;
      
    } else {
      status = 0;
      fprintf(stderr, "%s: Unknown mode '%s'\n", pModule, argv[1]);
    }
  }
  
  /* Get length of warp64 suffix */
  if (status) {
    suflen = strlen(FILE_SUFFIX);
  }
  
  /* Get the input file path and determine whether it has a .warp64
   * suffix */
  if (status) {
    pInputPath = argv[2];
    slen = strlen(pInputPath);
    if (slen > suflen) {
      if (strcmp(&(pInputPath[slen - suflen]), FILE_SUFFIX)
            == 0) {
        input_suffixed = 1;
      } else {
        input_suffixed = 0;
      }
      
    } else {
      input_suffixed = 0;
    }
  }
  
  /* Make sure presence of suffix matches the mode */
  if (status) {
    if (descramble) {
      if (!input_suffixed) {
        status = 0;
        fprintf(stderr, "%s: Input file must have .warp64 suffix!\n",
                pModule);
      }
      
    } else {
      if (input_suffixed) {
        status = 0;
        fprintf(stderr, "%s: Input file may not have .warp64 suffix!\n",
                pModule);
      }
    }
  }
  
  /* Make sure the input path is for an existing regular file */
  if (status) {
    if (stat(pInputPath, &st)) {
      status = 0;
      fprintf(stderr, "%s: Failed to stat '%s'\n",
              pModule, pInputPath);
    }
  }
  if (status) {
    if (!(S_ISREG(st.st_mode))) {
      status = 0;
      fprintf(stderr, "%s: '%s' is not a regular file\n",
              pModule, pInputPath);
    }
  }
  
  /* Derive the output file path */
  if (status && descramble) {
    /* We are descrambling, so we need to remove the .warp64 suffix */
    slen = strlen(pInputPath);
    if (slen <= suflen) {
      /* We already checked earlier that there is a warp64 suffix
       * present, so this shouldn't happen */
      abort();
    }
    
    /* The character before the suffix must not be a separator slash */
    if (pInputPath[slen - suflen - 1] == '/') {
      status = 0;
      fprintf(stderr, "%s: Invalid .warp64 suffix position!", pModule);
    }
    
    /* Allocate buffer for new path */
    if (status) {
      pOutputPath = (char *) calloc((slen - suflen) + 1, 1);
      if (pOutputPath == NULL) {
        abort();
      }
    }
    
    /* Copy the input path without the warp64 suffix */
    if (status) {
      memcpy(pOutputPath, pInputPath, slen - suflen);
    }
    
  } else if (status && (!descramble)) {
    /* We are scrambling, so we need to add a .warp64 suffix */
    slen = strlen(pInputPath);
    
    /* Allocate buffer for new path */
    pOutputPath = (char *) calloc(slen + suflen + 1, 1);
    if (pOutputPath == NULL) {
      abort();
    }
    
    /* Copy the input path and append the suffix */
    strcpy(pOutputPath, pInputPath);
    strcat(pOutputPath, FILE_SUFFIX);
  }
  
  /* Read the key */
  if (status) {
    printf("Enter scrambling key:\n");
    if (!readKey(&kb)) {
      status = 0;
    }
  }
  
  /* Call the main program function */
  if (status) {
    if (!warp64(pInputPath, pOutputPath, descramble, kb.kbuf)) {
      status = 0;
    }
  }
  
  /* Free output path if allocated */
  if (pOutputPath != NULL) {
    free(pOutputPath);
    pOutputPath = NULL;
  }
  
  /* Invert status and return */
  if (status) {
    status = 0;
  } else {
    status = 1;
  }
  return status;
}
