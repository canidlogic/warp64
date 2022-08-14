# Warp64

Warp64 is a binary scrambling algorithm.

__Warp64 is not an encryption algorithm!__  Do not use Warp64 in situations that require encryption!

Warp64 will scramble binary data so that it can't be opened in its scrambled state.  During scrambling, the user must choose a _scrambling key,_ which affects the manner in which the binary data is scrambled.  The scrambled data can then later be descrambled into its original state so that it can be opened again.  Descrambling, however, requires that the same scrambling key be provided that was used to scramble the data (or a scrambling key that is equivalent to the original scrambling key under the key normalization process described later).

Warp64 is also designed so that if the original scrambling key is lost, it is possible to recover an equivalent scrambling key by analyzing the scrambled data, by following a process described later.  This is why Warp64 is not an encryption algorithm.  In a proper encryption algorithm, if the key is lost, there should be no way to recover the key, and the data should then also be lost.  In Warp64, by contrast, it is possible to recover lost keys.

The intended use for Warp64 is when you want to weakly protect data such that it won't be accidentally or casually accessed when it is not supposed to be; however, you don't want the risk of losing your data if you forget the key.  Warp64 protects against non-malicious access, but offers no protection against malicious access, sort of like putting a simple lock on a door that could easily be picked or broken by an adversary who was determined to get through the door.  Warp64 also might avoid certain legal hazards associated with using strong encryption algorithms, since it should be trivial for governments and law enforcement to descramble any Warp64 data by using the key recovery procedure described later.

## Caveats

Please read and understand the following caveat subsections before using Warp64.

### Not an encryption algorithm

Re-read the opening section of this document and make sure you understand that _Warp64 is not an encryption algorithm_ and _Warp64 will not protect your data and secrets from adversaries_ before using Warp64.

### Password safety

Since scrambling keys can be recovered, _never use a secure password as a scrambling key._  You should assume that any password used as a scrambling key is immediately compromised, because this is similar to storing a password in plain-text.

### Raw multimedia

Warp64 may not function as intended if it is used on certain _raw multimedia streams._  For sake of this section, a raw multimedia stream is defined as follows:

1. The file format has no required headers, __AND__
2. The data is multimedia samples (pixels, audio, etc.), __AND__
3. No compression is used.

It is unusual to work with data files that fit this definition of a raw multimedia stream.  Most uncompressed multimedia files will still have at least one required header.  Therefore, uncompressed WAV, BMP, and AVI files do _not_ count as raw multimedia streams because they each have a required header.  Examples of raw multimedia streams would be an intermediate recording file that just stores audio samples and nothing else, or a raw RGB stream.  These types of resources are generally temporary in nature and so it would be unusual to want to scramble and store them.

Plain-text files do __not__ count as raw multimedia streams.  Warp64 _will_ work properly on plain-text files.  Even though such files have no required headers, and such files are not compressed, and you could possibly argue that the individual characters or codepoints are a kind of multimedia sample, Warp64 scrambling will still render plain-text files inaccessible.

The problem with raw multimedia streams is that the transformation Warp64 applies might only add a bit of noise to the data.  For example, if a raw RGB video stream were scrambled with Warp64, it would still be possible to play it back in its scrambled form, and the original content of the video stream might still be visible (with the scrambling just appearing as a form of noise).

However, the moment a data file has a required header, Warp64's scrambling operation should make the header unreadable, which will cause programs to fail to load the scrambled file.

Also, the possibility of Warp64 just adding noise does not apply to plain-text files.  Changing all the symbols in a document should always make the document illegible.

## Key normalization

Both scrambling and descrambling require a scrambling key.  This key is a sequence of one or more base-64 characters.  _Key normalization_ takes this sequence of base-64 characters and normalizes it into exactly three octets using the normalization algorithm described in this section.

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

This mixed key with replacement is then the result of the key normalization process.  Two different scrambling keys that normalize to the same three octets are equivalent as far as Warp64 is concerned.

## Scrambling algorithm

The input is a sequence of zero or more binary octets, as well as a sequence of one or more base-64 characters that will serve as the scrambling key.

The input is first suffixed with three octets, each of which has value zero.  This is done to make key recovery easy (see later).

Second, the provided scrambling key is normalized into exactly three octets using the key normalization algorithm described previously.  Let `z_0` `z_1` and `z_2` be the three octets of the normalized key.

Third, each octet of input is scrambled in the following way.  Let `t_0` `t_1` ... `t_n` be the octets of the input, which includes the three octets suffixed at the end in the first step.  Then, define the scrambled octets `v_0` `v_1` ... `v_n` as follows:

    v_i := (t_i + z_(i MOD 3)) MOD 256

The output of the scrambling algorithm is the scrambled octets.

## Descrambling algorithm

The input is a sequence of three or more binary octets, as well as a sequence of one or more base-64 characters that was used as the scrambling key.  Since scrambled data always has an three-octet suffix, the length of scrambled input must always be at least eight.

The provided scrambling key is first normalized into exactly three octets `z_0` `z_1` `z_2` using the key normalization algorithm described previously.

Second, the normalized scrambling key octets are transformed into descrambling octets `w_0` `w_1` `w_2` using the following formula:

    w_j := 256 - z_j

(Normalized scrambling key octets are never zero, so the result of this formula will always remain in range [1, 255].)

Third, each octet of input is descrambled in the following way.  Let `v_0` `v_1` ... `v_n` be the octets of the input.  Then, define the descrambled octets `t_0` `t_1` ... `t_n` as follows:

    t_i := (v_i + w_(i MOD 3)) MOD 256

Fourth, the unsigned values of the last three descrambled octets are checked that they are all zero.  Otherwise, the wrong scrambling key was provided.

The descrambled output is anything that precedes the last three octets.

## Key recovery

This section describes how to recover a lost scrambling key for a scrambled Warp64 file.  The recovered key is not necessarily the same as the original scrambling key that was used, but it will be equivalent under key normalization, so that it will be able to successfully descramble the data.

The first step of key recovery is to get the values of the last three octets in the file.

The second step of key recovery is to reorder these last three octets.  Let `m_0` `m_1` and `m_2` be the last three octets in the file, and `n_0` `n_1` `n_2` be the re-ordered octets.  Let `a` be the byte offset of `m_0` in the file, such that the first byte of the file is byte offset zero, the second byte of the file is byte offset one, and so forth.  Then, the following formula will re-order the three octets:

    n_i := m_((z + i) MOD 3)
    z   := 3 - (a MOD 3)

Once the three octets are ordered, apply a base-64 transformation to write `n_0` `n_1` and `n_2` as exactly four base-64 digits.  The result of this will be a scrambling key that is equivalent to the original scrambling key under key normalization.
