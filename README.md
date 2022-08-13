# Warp64

Warp64 is a binary scrambling algorithm.

Warp64 will scramble digital data so that it can't be opened.  Warp64 is then later able to descramble digital data back to its original state so that it can be opened again.  During scrambling, a _key_ of one or more base-64 characters must be provided.  A matching key must be used during descrambling to get the original data back.

__Warp64 is NOT encryption.  Do not use Warp64 in applications that require data be securely encrypted.__  By design, it is easy to recover lost keys, so you can't rely on keys to provide any sort of security whatsoever.  Warp64 is intended for applications where you want to alter the data such that it can't be accidentally opened while scrambled, but you don't want to worry about losing data due to forgotten encryption keys.

Also, __Warp64 may not work on raw, uncompressed multimedia streams.__  This refers to file types such as raw PCM sound recordings without any header, or raw RGB files without any headers.  (Such files are not typically used for storage.)  If a file has any sort of headers (which is usually the case), then Warp64 should work because the headers will be scrambled.  It's only in the unusual case when you're dealing with sampling streams that don't have a header when Warp64 may fail to make the file impossible to open, because in these cases the transformations that Warp64 applies might just show up as a bit of noise.  Raw text files should always work with Warp64, though, even if they don't have any sort of header.

## Key normalization

Both scrambling and descrambling require a key to be provided.  This key is a sequence of one or more base-64 characters.  _Key normalization_ takes this sequence of base-64 characters and normalizes it into exactly four base-64 characters using the normalization algorithm described in this section.

Base-64 characters are chosen from the following alphabet of 64 characters:

    ABCDEFGHIJKLMNOPQRSTUVWXYZ
    abcdefghijklmnopqrstuvwxyz
    0123456789+/

The input to key normalization is a sequence of one or more of these characters.

The first step in key normalization is to extend the key so that its length is a multiple of four.  This requires at most three additional base-64 characters to be appended.  Let `x_0` `x_1` `x_2` be the three additional base-64 characters that can be appended.  Let `k_0` `k_1` ... `k_(n-1)` be the base-64 characters in the original key, with `n` being the length in characters of the original key.  Then, the following formula derives the additional base-64 characters:

    x_i = k_(i MOD n)

In effect, this formula will use the first three characters of the original key, unless the original key had a length less than three.  If the original key had a length of two, then this formula will use the first character, the second character, and then the first character again.  If the original key had a length of one, then this formula will just repeat the first character.

Examples of key extension:

    C       -> CCCC
    Example -> ExampleE
    Dog12   -> Dog12Dog
    12      -> 1212

After the key length has been extended to a multiple of four, the second step in key normalization is to XOR the extended key down to a length of exactly four.  If the extended key is already length four, then this step does nothing.  Otherwise, it divides the extended key into _segments_ of exactly four base-64 digits each, `s_0` `s_1` ... `s_m` where `m` is the extended key length in characters divided by four.  Each of these segments of four base-64 digits is decoded to exactly three octets using base-64 decoding.  Then, the _mixed key_ is derived by bitwise XOR as follows:

    mixed := (...((s_0 XOR s_1) XOR s_2) ... XOR s_m)

This mixed key is exactly three octets, which is exactly 24 bits.

Once the mixed key is obtained, the third step in key normalization is to perform _octet replacement_.  Octet replacement replaces any octet in the mixed key that has a value of zero with a non-zero value `r_j` where `j` is either 0, 1, or 2 for the first, second, or third octet of the mixed key.  The following values are used:

    r_0 := 1
    r_1 := 2
    r_2 := 4

The fourth and final step in key normalization is to encode the three octets of the replaced mixed key as exactly four base-64 digits.

## Scrambling algorithm

The input is a sequence of zero or more binary octets, as well as a sequence of one or more base-64 characters that will serve as the scrambling key.

The provided scrambling key is first normalized into exactly four base-64 digits using the key normalization algorithm described above.

Second, the normalized scrambling key is decoded into exactly three non-zero octets `z_0` `z_1` `z_2` using base-64 decoding.

Third, each octet of input is scrambled in the following way.  Let `t_0` `t_1` ... `t_n` be the octets of the input.  Then, define the scrambled octets `v_0` `v_1` ... `v_n` as follows:

    v_i := (t_i + z_(i MOD 3)) MOD 256

The output of the scrambling algorithm is the scrambled octets.

## Descrambling algorithm

The input is a sequence of zero or more binary octets, as well as a sequence of one or more base-64 characters that was used as the scrambling key.

The provided scrambling key is first normalized into exactly four base-64 digits using the key normalization algorithm described above.

Second, the normalized scrambling key is decoded into exactly three non-zero octets `z_0` `z_1` `z_2` using base-64 decoding.

Third, the normalized scrambling key octets are transformed into descrambling octets `w_0` `w_1` `w_2` using the following formula:

    w_j := 256 - z_j

(Normalized scrambling key octets are never zero, so the result of this formula will always remain in range [1, 255].)

Fourth, each octet of input is descrambled in the following way.  Let `v_0` `v_1` ... `v_n` be the octets of the input.  Then, define the descrambled octets `t_0` `t_1` ... `t_n` as follows:

    t_i := (v_i + w_(i MOD 3)) MOD 256

The output of the descrabmling algorithm is the descrambled octets.  It the same scrambling key was provided to the descrambling algorithm that was used during scrambling, then the descrambled octets will be exactly the same as the original octets prior to scrambling.
