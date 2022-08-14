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
 * 
 * -d is descrambling mode.  The input file path must end with
 * ".warp64".  The output path is the same as the input path, except
 * that ".warp64" is dropped from the end of it.
 * 
 * For both scrambling and descrambling, the output file path must NOT
 * exist yet or the program will fail.  For both scrambling and
 * descrambling, if the operation is successful, the input file will be
 * deleted at the end of the operation.
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

/* POSIX headers */
#include <fcntl.h>
#include <sys/mman.h>
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
 * 
 * The actual size of a memory-mapped window is computed at the start of
 * the entrypoint and stored in m_winsize.
 * 
 * m_winsize is the actual memory-mapped window size.  WINDOW_TARGET is
 * only used near the start of the entrypoint for computing m_winsize.
 */
#define WINDOW_TARGET (4194304L)

/*
 * Data types
 * ==========
 */

/*
 * Stores a nul-terminated scrambling key.
 */
typedef struct {

  /*
   * Buffer holding the scrambling key that was read.
   * 
   * The key must be nul-terminated.
   */
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
static int decode64(int c);
static int readKey(KEY_BUFFER *kb);
static int32_t deriveKey(const char *pKey);

static int process64(
    int     fIn,
    int     fOut,
    int32_t key,
    int     trailer,
    int64_t olen);

static int warp64(
    const char * pInputPath,
    const char * pOutputPath,
          int    descramble,
    const char * pKey);

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
 * Read the scrambling key from standard input, suppressing echo so that
 * the key is not displayed.
 * 
 * Error messages are printed if failure.  This function will check that
 * each character read decodes with decode64(), and that at least one
 * and at most MAX_KEY_LENGTH characters are read.
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
  
  /* Update console input immediately to disable echo and then set the
   * console_changed flag */
  if (status) {
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_attr)) {
      status = 0;
      fprintf(stderr, "%s: Failed to set console input attributes!\n",
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
    if (status && ((c < 0) || (c > 128))) {
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
        fprintf(stderr, "%s: Key may only include A-Z a-z 0-9 + /\n",
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
 * Use memory-mapping to perform Warp64 scrambling or descrambling.
 * 
 * fIn and fOut are the file descriptors for the input file and the
 * output file, respectively.
 * 
 * key contains the scrambling or descrambling key in the 24 least
 * significant bits.  If you are scrambling, this should be equal to the
 * normalized scrambling key.  If you are descrambling, each component
 * byte should be adjusted to invert the scrambling process.
 * 
 * trailer is non-zero if the input should be treated as if it had three
 * octets of zero value suffixed to it.  This should be set during
 * scrambling so that the trailer gets written.
 * 
 * olen is the length in bytes of the output file.  For scrambling, this
 * includes the three trailer bytes.  olen must be greater than zero.
 * If trailer is non-zero, olen must be at least three.
 * 
 * Error messages are printed.
 * 
 * Parameters:
 * 
 *   fIn - the input file descriptor
 * 
 *   fOut - the output file descriptor
 * 
 *   key - the (de)scrambling key
 * 
 *   trailer - non-zero for three extra zero octets after input
 * 
 *   olen - the length of output in bytes
 * 
 * Return:
 * 
 *   non-zero if successful, zero if error
 */
static int process64(
    int     fIn,
    int     fOut,
    int32_t key,
    int     trailer,
    int64_t olen) {
  
  int status = 1;
  int32_t i = 0;
  int32_t j = 0;
  int k = 0;
  
  int64_t base = 0;
  int64_t remc = 0;
  int64_t remi = 0;
  
  int32_t ws = 0;
  int32_t wsi = 0;
  
  uint8_t kb[3];
  uint8_t *pd[3];
  uint8_t *pwo = NULL;
  uint8_t *pwi = NULL;
  
  /* Initialize arrays */
  memset(kb, 0, 3);
  
  memset(pd, 0, sizeof(uint8_t *) * 3);
  pd[0] = NULL;
  pd[1] = NULL;
  pd[2] = NULL;
  
  /* Check parameters */
  if ((fIn < 0) || (fOut < 0) || (olen < 1)) {
    abort();
  }
  if (trailer && (olen < 3)) {
    abort();
  }
  
  /* Unpack key into buffer */
  kb[0] = (uint8_t) ((key >> 16) & 0xff);
  kb[1] = (uint8_t) ((key >>  8) & 0xff);
  kb[2] = (uint8_t) ( key        & 0xff);
  
  /* Allocate and initialize the key dictionaries */
  for(i = 0; i < 3; i++) {
    /* Allocate key dictionary */
    pd[i] = (uint8_t *) calloc(256, 1);
    if (pd[i] == NULL) {
      abort();
    }
    
    /* Initialize entries according to the proper key component */
    for(j = 0; j < 256; j++) {
      (pd[i])[j] = (uint8_t) ((j + ((int) kb[i])) % 256);
    }
  }
  
  /* Start at offset zero in output and initialize remaining byte count
   * to the given size of output */
  base = 0;
  remc = olen;
  
  /* Remaining input is same as remaining output byte count, except when
   * trailer is active, in which case remaining input is three less than
   * remaining output */
  remi = remc;
  if (trailer) {
    remi = remi - 3;
  }
  
  /* Keep processing until we have no bytes left */
  while (status && (remc > 0)) {
    /* First, determine the size of the current window we are dealing
     * with; this is the minimum of the window size and the remaining
     * bytes */
    ws = (int32_t) m_winsize;
    if (remc < ws) {
      ws = (int32_t) remc;
    }
    
    /* Second, determine the size of the current input window; this is
     * the minimum of the current output window and the remaining input
     * count; it might be zero */
    wsi = ws;
    if (remi < wsi) {
      wsi = (int32_t) remi;
    }
    
    /* Map the current output window */
    pwo = (uint8_t *) mmap(
                        NULL,
                        (size_t) ws,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED,
                        fOut,
                        (off_t) base);
    if ((pwo == MAP_FAILED) || (pwo == NULL)) {
      status = 0;
      fprintf(stderr, "%s: Failed to map output window!\n", pModule);
    }
    
    /* Map the current input window if non-empty */
    if (status && (wsi > 0)) {
      pwi = (uint8_t *) mmap(
                          NULL,
                          (size_t) wsi,
                          PROT_READ,
                          MAP_PRIVATE,
                          fIn,
                          (off_t) base);
      if ((pwi == MAP_FAILED) || (pwi == NULL)) {
        status = 0;
        fprintf(stderr, "%s: Failed to map input window!\n", pModule);
      }
    }
    
    /* Compute all the bytes in the current output window */
    if (status) {
      /* Start the byte dictionary index out at the proper position for
       * the first byte of the window */
      k = (int) (base % 3);
      
      /* Compute all bytes */
      for(i = 0; i < ws; i++) {
    
        /* Read the current byte value from the input window, unless i
         * is outside the range of the current input window, in which
         * case just use a byte value of zero (for the trailer) */
        j = 0;
        if (i < wsi) {
          j = (int32_t) pwi[i];
        }
    
        /* Replace current byte value in output with its value from the
         * byte dictionary */
        j = (int32_t) ((pd[k])[j]);
        
        /* Increment byte dictionary index and apply mod-3 */
        k = ((k + 1) % 3);
    
        /* Write the transformed byte value to output window */
        pwo[i] = (uint8_t) j;
      }
    }
    
    /* Unmap current input window if it was mapped */
    if (status && (wsi > 0)) {
      if (munmap(pwi, (size_t) wsi)) {
        status = 0;
        fprintf(stderr, "%s: Failed to unmap input window!\n",
                pModule);
      }
      pwi = NULL;
    }
    
    /* Unmap current output window */
    if (status) {
      if (munmap(pwo, (size_t) ws)) {
        status = 0;
        fprintf(stderr, "%s: Failed to unmap output window!\n",
                pModule);
      }
      pwo = NULL;
    }
    
    /* Update base remc and remi before going around again */
    if (status) {
      base = base + ((int64_t) ws);
      remc = remc - ((int64_t) ws);
      remi = remi - ((int64_t) ws);
      if (remi < 0) {
        remi = 0;
      }
    }
  }
  
  /* If input window is mapped, unmap it */
  if (pwi != NULL) {
    if (munmap(pwi, (size_t) wsi)) {
      fprintf(stderr, "%s: Failed to unmap input window!\n", pModule);
    }
    pwi = NULL;
  }
  
  /* If output window is mapped, unmap it */
  if (pwo != NULL) {
    if (munmap(pwo, (size_t) ws)) {
      fprintf(stderr, "%s: Failed to unmap output window!\n", pModule);
    }
    pwo = NULL;
  }
  
  /* Release byte dictionaries if allocated */
  if (pd[0] != NULL) {
    free(pd[0]);
    pd[0] = NULL;
  }
  if (pd[1] != NULL) {
    free(pd[1]);
    pd[1] = NULL;
  }
  if (pd[2] != NULL) {
    free(pd[2]);
    pd[2] = NULL;
  }
  
  /* Return status */
  return status;
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
  int i = 0;
  int z = 0;
  
  int32_t key = 0;
  int32_t tk = 0;
  int64_t ctlen = 0;
  int64_t olen = 0;
  
  uint8_t trailer[3];
  uint8_t kb[3];
  uint8_t dummy = 0;
  
  int new_file = 0;
  int fIn = -1;
  int fOut = -1;
  
  /* Initialize structures */
  memset(trailer, 0, 3);
  memset(kb, 0, 3);
  
  /* Check parameters */
  if ((pInputPath == NULL) || (pOutputPath == NULL) || (pKey == NULL)) {
    abort();
  }
  
  /* Derive the normalized key */
  key = deriveKey(pKey);
  if (key < 0) {
    status = 0;
  }
  
  /* Open the input file for reading */
  if (status) {
    fIn = open(pInputPath, O_RDONLY);
    if (fIn < 0) {
      status = 0;
      fprintf(stderr, "%s: Failed to open '%s'\n", pModule, pInputPath);
    }
  }
  
  /* Get the length of the input file */
  if (status) {
    ctlen = (int64_t) lseek(fIn, 0, SEEK_END);
    if (ctlen < 0) {
      status = 0;
      fprintf(stderr, "%s: Failed to get length of '%s'!\n",
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
   * least three and then content length is three less than input file;
   * else, leave content length equal to input file length */
  if (status && descramble) {
    if (ctlen < 3) {
      status = 0;
      fprintf(stderr, "%s: Missing trailer in '%s'!\n",
              pModule, pInputPath);
    }
    if (status) {
      ctlen = ctlen - 3;
    }
  }
  
  /* If this is descrambling mode, then read the last three bytes and
   * verify the descrambled trailer bytes are zero to check the key */
  if (status && descramble) {
    /* Read the last three bytes into trailer */
    if (lseek(fIn, -3, SEEK_END) < 0) {
      status = 0;
      fprintf(stderr, "%s: Failed to seek to trailer in '%s'!\n",
              pModule, pInputPath);
    }
    
    if (status) {
      if (read(fIn, trailer, 3) != 3) {
        status = 0;
        fprintf(stderr, "%s: Failed to read trailer in '%s'!\n",
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
    
    /* Figure out the index of the scrambling key to use for the first
     * byte of the head */
    if (status) {
      z = (int) (3 - ((ctlen - 3) % 3));
    }
    
    /* Pack the properly re-ordered trailer bytes into an integer to
     * figure out the normalized key that must be used */
    if (status) {
      tk = 0;
      for(i = 0; i < 3; i++) {
        tk = (tk << 8) | ((int32_t) trailer[(z + i) % 3]);
      }
    }
    
    /* Verify that the provided scrambling key is correct */
    if (status) {
      if (key != tk) {
        status = 0;
        fprintf(stderr, "%s: Incorrect scrambling key!\n", pModule);
      }
    }
  }
  
  /* Open the output file for writing; do not allow existing files to be
   * overwritten; set new_file flag if successfully created a new 
   * file */
  if (status) {
    fOut = open(pOutputPath, O_RDWR | O_CREAT | O_EXCL,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fOut < 0) {
      status = 0;
      fprintf(stderr, "%s: Failed to create '%s'!\n",
              pModule, pOutputPath);
      fprintf(stderr, "%s: Check that '%s' does not exist.\n",
              pModule, pOutputPath);
    }

    if (status) {
      new_file = 1;
    }
  }
  
  /* Compute length of output file based on content length */
  if (status && descramble) {
    olen = ctlen;
    
  } else if (status && (!descramble)) {
    if (ctlen <= INT64_MAX - 3) {
      olen = ctlen + 3;
    } else {
      status = 0;
      fprintf(stderr, "%s: Output file length overflow!\n", pModule);
    }
    
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
      if (write(fOut, &dummy, 1) != 1) {
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
    
    /* If we are descrambling, turn the scrambling key into a
     * descrambling key */
    if (status && descramble) {
      
      /* Unpack the key into a buffer */
      kb[0] = (uint8_t) ((key >> 16) & 0xff);
      kb[1] = (uint8_t) ((key >>  8) & 0xff);
      kb[2] = (uint8_t) ( key        & 0xff);
      
      /* Invert each component byte */
      for(i = 0; i < 3; i++) {
        /* Component byte should not be zero */
        if (kb[i] == 0) {
          abort();
        }
        
        /* Invert component byte */
        kb[i] = (uint8_t) (256 - ((int) kb[i]));
      }
      
      /* Pack buffer back into key */
      key =   (((int32_t) kb[0]) << 16)
            | (((int32_t) kb[1]) <<  8)
            | ( (int32_t) kb[2]       );
    }
  
    /* Process the file */
    if (status && descramble) {
      if (!process64(fIn, fOut, key, 0, olen)) {
        status = 0;
      }
      
    } else if (status && (!descramble)) {
      if (!process64(fIn, fOut, key, 1, olen)) {
        status = 0;
      }
      
    } else if (status) {
      abort();
    }
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
  
  /* If we got here successfully, remove the input file */
  if (status) {
    if (unlink(pInputPath)) {
      fprintf(stderr, "%s: Failed to remove input file!\n", pModule);
    }
  }
  
  /* Return status */
  return status;
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
