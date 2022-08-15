"use strict";

/*
 * warp64.js
 * =========
 * 
 * Service worker for performing Warp64 scrambling and descrambling.
 * 
 * Input messages are objects containing:
 * 
 *   descramble : boolean - true if descrambling, false if scrambling
 *   key : string - the scrambling key
 *   data : ArrayBuffer - the input file data
 * 
 * IMPORTANT:  When you post the message, you must transfer ownership
 * of the data ArrayBuffer.
 * 
 * Output messages are objects containing:
 * 
 *   status : boolean - true if successful, false if error
 *   errDiv : string - the DIV id of the error div to show if error
 *   data : ArrayBuffer - the transformed data if successful
 */

/*
 * Fault function
 * ==============
 */

/*
 * Report an error to console and throw an exception for a fault 
 * occurring within this module.
 *
 * Parameters:
 *
 *   func_name : string - the name of the function in this module
 *
 *   loc : number(int) - the location within the function
 */
function fault(func_name, loc) {
  
  // If parameters not valid, set to unknown:0
  if ((typeof func_name !== "string") || (typeof loc !== "number")) {
    func_name = "unknown";
    loc = 0;
  }
  loc = Math.floor(loc);
  if (!isFinite(loc)) {
    loc = 0;
  }
  
  // Report error to console
  console.log("Fault at " + func_name + ":" + String(loc) +
                " in warp64.js");
  
  // Throw exception
  throw ("warp64js:" + func_name + ":" + String(loc));
}

/*
 * Local functions
 * ===============
 */

/*
 * Given a one-character string, return the decoded base-64 value.
 *
 * Parameters:
 *
 *   c : string - a one-character string
 *
 * Return:
 *
 *   the numeric base-64 value or -1 if this isn't a base-64 digit
 */
function decode64(c) {
  var func_name = "decode64";
  var result;
  
  // Check parameter
  if (typeof(c) !== "string") {
    fault(func_name, 100);
  }
  if (c.length !== 1) {
    fault(func_name, 101);
  }
  
  // Get the character code
  c = c.charCodeAt(0);
  
  // Decode
  if ((c >= 0x41) && (c <= 0x5a)) {
    result = c - 0x41;
    
  } else if ((c >= 0x61) && (c <= 0x7a)) {
    result = (c - 0x61) + 26;
    
  } else if ((c >= 0x30) && (c <= 0x39)) {
    result = (c - 0x30) + 52;
    
  } else if (c === 0x2b) {
    result = 62;
    
  } else if (c == 0x2f) {
    result = 63;
    
  } else {
    result = -1;
  }
  
  // Return result
  return result;
}

/*
 * Given a scrambling key of one or more base-64 characters, derive the
 * normalized scrambling key and return it.
 * 
 * The three octets of the normalized scrambling key are returned in an
 * array of three elements.
 * 
 * Parameters:
 * 
 *   str - the base-64 string defining the key
 * 
 * Return:
 * 
 *   the three normalized octets in an array, or undefined if error
 */
function deriveKey(str) {
  var func_name = "deriveKey";
  var ext, mixed, b64, acc, i, d, result;
  
  // Check parameter
  if (typeof(str) !== "string") {
    fault(func_name, 100);
  }
  
  // Fail if wrong length
  if ((str.length < 1) || (str.length > 255)) {
    return undefined;
  }
  
  // Determine the three extension characters that will be used if
  // padding is necessary
  if (str.length === 1) {
    ext = [str.charAt(0), str.charAt(0), str.charAt(0)];
  
  } else if (str.length === 2) {
    ext = [str.charAt(0), str.charAt(1), str.charAt(0)];
    
  } else if (str.length >= 3) {
    ext = [str.charAt(0), str.charAt(1), str.charAt(2)];
    
  } else {
    fault(func_name, 200);
  }
  
  // Keep updating the result until we have processed all of the key
  mixed = 0;
  while (str.length > 0) {
    // Fill b64 buffer with four base-64 characters and update str,
    // using extension characters if necessary
    if (str.length === 1) {
      // We have one more character, so use that and three
      // extensions
      b64 = [str.charAt(0), ext[0], ext[1], ext[2]];
      str = "";
      
    } else if (str.length === 2) {
      // We have two more characters, so use those and two
      // extensions
      b64 = [str.charAt(0), str.charAt(1), ext[0], ext[1]];
      str = "";
      
    } else if (str.length === 3) {
      // We have three more characters, so use those and one
      // extension
      b64 = [str.charAt(0), str.charAt(1), str.charAt(2), ext[0]];
      str = "";
      
    } else if (str.length >= 4) {
      // We have at least four more characters, so just use those
      b64 = [
            str.charAt(0), str.charAt(1), str.charAt(2), str.charAt(3)];
      str = str.slice(4);
      
    } else {
      abort();
    }
    
    // Decode the four base-64 characters into a packed integer
    acc = 0;
    for(i = 0; i < 4; i++) {
      d = decode64(b64[i]);
      if (d < 0) {
        return undefined;
      }
      acc = (acc << 6) | d;
    }
    
    // XOR in the new segment into the mixed key
    mixed = mixed ^ acc;
  }
  
  // Unpack the mixed key into the result buffer
  result = [
    (mixed >> 16) & 0xff,
    (mixed >>  8) & 0xff,
     mixed        & 0xff
  ];
  
  // Perform replacements to make sure no zero components
  if (result[0] === 0) {
    result[0] = 1;
  }
  if (result[1] === 0) {
    result[1] = 2;
  }
  if (result[2] === 0) {
    result[2] = 4;
  }
  
  // Return result
  return result;
}

/*
 * Perform the actual Warp64 scrambling or descrambling operation.
 * 
 * Parameters:
 * 
 *   descramble : boolean - true to descramble, false to scramble
 * 
 *   key : string - the scrambling key
 * 
 *   data : ArrayBuffer - the input data
 * 
 * Return:
 * 
 *   ArrayBuffer containing the result, or a string containing a DIV ID
 *   of an error DIV if there was an error
 */
function warp64(descramble, key, data) {
  var func_name = "warp64";
  var kb, trail, tk, i, z;
  var dest, srci, src, target, db;
  
  // Check parameters
  if (typeof(descramble) !== "boolean") {
    fault(func_name, 100);
  }
  if (typeof(key) !== "string") {
    fault(func_name, 101);
  }
  if (!(data instanceof ArrayBuffer)) {
    fault(func_name, 102);
  }
  
  // Derive normalized key
  kb = deriveKey(key);
  if (!kb) {
    return "divKeyError";
  }
  
  // Special cases -- if descrambling mode and length is three, or if
  // scrambling mode and length is zero, then just return special empty
  // states
  if (descramble && (data.byteLength === 3)) {
    // Descrambling file of length three, so just return empty array
    // buffer
    return new ArrayBuffer(0);
    
  } else if ((!descramble) && (data.byteLength < 1)) {
    // Scrambling file of length zero, so return is array with just the
    // three normalized octets
    dest = new ArrayBuffer(3);
    target = new Uint8Array(dest);
    target[0] = kb[0];
    target[1] = kb[1];
    target[2] = kb[2];
    return dest;
  }
  
  // If this is descrambling mode, we must have at least three bytes of
  // data
  if (descramble) {
    if (data.byteLength < 3) {
      return "divBadError";
    }
  }
  
  // If this is descrambling mode, then check that the scrambling key is
  // correct
  if (descramble) {
    // Get the trailer bytes
    trail = new Uint8Array(data, data.byteLength - 3, 3);
    
    // Figure out the index of the scrambling key to use for the first
    // byte of the trailer
    z = 3 - ((data.byteLength - 3) % 3);
    
    // Get the properly reordered scrambling key bytes
    tk = [];
    for(i = 0; i < 3; i++) {
      tk.push(trail[(z + i) % 3]);
    }
    
    // Verify that given scrambling key is correct
    for(i = 0; i < 3; i++) {
      if (kb[i] !== tk[i]) {
        return "divWrongError";
      }
    }
  }
  
  // If this is descrambling mode, invert the scrambling key bytes
  if (descramble) {
    for(i = 0; i < 3; i++) {
      kb[i] = 256 - kb[i];
    }
  }
  
  // Define the destination array buffer with the proper length
  if (descramble) {
    dest = new ArrayBuffer(data.byteLength - 3);
  } else {
    dest = new ArrayBuffer(data.byteLength + 3);
  }
  
  // Define the byte value lookup tables
  db = [ [], [], [] ];
  for(i = 0; i < 3; i++) {
    for(z = 0; z < 256; z++) {
      (db[i]).push((z + kb[i]) % 256);
    }
  }
  
  // Process according to up to two different sources
  for(srci = 0; srci < 2; srci++) {
    // Get the appropriate source buffer
    if (srci === 0) {
      // First time around is always the main data, which excludes the
      // trailer in descrambling mode
      if (descramble) {
        src = new Uint8Array(data, 0, data.byteLength - 3);
      } else {
        src = new Uint8Array(data);
      }
      
    } else if (srci === 1) {
      // Second time around in scrambling mode is three zero bytes,
      // second pass is skipped in descrambling mode
      if (descramble) {
        break;
      } else {
        src = new Uint8Array(3);
      }
      
    } else {
      fault(func_name, 200);
    }
    
    // Get the appropriate target buffer
    if (srci === 0) {
      // First time around always starts at beginning and runs to same
      // length as source
      target = new Uint8Array(dest, 0, src.length);
      
    } else if (srci === 1) {
      // Second time around only occurs in scrambling mode, starts three
      // bytes from end, and runs to end
      if (descramble) {
        fault(func_name, 220);
      } else {
        target = new Uint8Array(dest, dest.byteLength - 3, 3);
      }
      
    } else {
      fault(func_name, 201);
    }
    
    // Set the opening offset z
    if (srci === 0) {
      z = 0;
    } else if (srci === 1) {
      z = data.byteLength;
    } else {
      fault(func_name, 202);
    }
    
    // Now transform from source to target
    for(i = 0; i < src.length; i++) {
      target[i] = db[(z + i) % 3][src[i]];
    }
  }
  
  // If we got here, return the destination result
  return dest;
}

/*
 * Service worker message handler
 * ==============================
 */

onmessage = function(e) {
  var func_name = "onmessage";
  var msg, retval;
  
  // Get our message
  if (typeof(e) !== "object") {
    fault(func_name, 50);
  }
  if (!("data" in e)) {
    fault(func_name, 51);
  }
  msg = e.data;
  
  // Check message
  if (typeof(msg) !== "object") {
    fault(func_name, 100);
  }
  if (!("descramble" in msg)) {
    fault(func_name, 101);
  }
  if (!("key" in msg)) {
    fault(func_name, 102);
  }
  if (!("data" in msg)) {
    fault(func_name, 103);
  }
  
  if (typeof(msg.descramble) !== "boolean") {
    fault(func_name, 104);
  }
  if (typeof(msg.key) !== "string") {
    fault(func_name, 105);
  }
  if (!(msg.data instanceof ArrayBuffer)) {
    fault(func_name, 106);
  }
  
  // Call through to processing function
  retval = warp64(msg.descramble, msg.key, msg.data);
  
  // Send proper response message
  if (retval instanceof ArrayBuffer) {
    postMessage({
      "status": true,
      "data": retval
    }, [retval]);
    
  } else if (typeof(retval) === "string") {
    postMessage({
      "status": false,
      "errDiv": retval
    });
    
  } else {
    fault(func_name, 200);
  }
};
