/* This file contains a variety of implementations of memset. If you don't know
 * what memset is `man memset` may enlighten you. Its definition is
 *
 *  void* memset(void* b, int c, size_t len);
 *
 * I wrote this code partly as a gentle introduction to memset and partly as a
 * reminder to myself of how memset works. This code is in the public domain and
 * you are free to use it for any purpose (commercial or non-commercial) with or
 * without attribution to me. I don't guarantee the correctness of any of the
 * code herein and if you spot a mistake please let me know and I'll correct it.
 *
 * I wrote this code during my free time, but at a period during which I was
 * employed by National ICT Australia and it was heavily related to my work. If
 * push comes to shove it may be considered their intellectual property, but as
 * it is not functional freestanding code and (better) implementations of memset
 * are widely available, I hope they won't mind :)
 *
 * Matthew Fernandez, 2011
 */

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

/* Just for convenience let's setup a type for bytes. */
typedef char byte;

/* Let's start off with a fairly naive implementation of memset. This sets
 * memory byte-by-byte. While not being particularly efficient and being
 * slightly braindead, it does have the advantage of being readily
 * understandable and reasonably straightforward to implement without making
 * mistakes.
 */
void* bytewise_memset(void* s, int c, size_t sz) {
    byte* p = (byte*)s;

    /* c should only be a byte's worth of information anyway, but let's mask out
     * everything else just in case.
     */
    byte x = c & 0xff;

    while (sz--)
        *p++ = x;
    return s;
}

/* A smarter way of doing memset is word-by-word. Your architecture will have a
 * native word size (32 bits on x86) and reads/writes at this granularity will
 * be more efficient than those at finer granularities. Let's take a look at
 * memset for a 32-bit architecture.
 */
void* wordwise_32_memset(void* s, int c, size_t sz) {
    uint32_t* p = (uint32_t*)s;

    /* In this case the masking is actually important. */
    uint32_t x = c & 0xff;

    /* Construct a word's worth of the value we're supposed to be setting. */
    x |= x << 8;
    x |= x << 16;

    /* This technique (without a prologue and epilogue) will only cope with
     * sizes that are word-aligned. For example, you cannot use this function to
     * set a region 7 bytes long. Let's do some checks to make sure the size
     * passed is actually something we can cope with. It is worth noting that in
     * practice you would actually want to check the alignment of the start
     * pointer as well. Doing a word-wise memset on an unaligned pointer gains
     * you nothing and may even hurt performance.
     */
    assert(!(sz & 3)); /* Check sz is 4-byte-aligned. */
    sz >>= 2; /* Divide by number of bytes in a word. */

    while (sz--)
        *p++ = x;
    return s;
}

/* Now let's do an architecture-independent word-by-word memset. You would never
 * want to implement this in practice, as you are relying on the compiler to
 * optimise the statically determinable loops that could be manually written
 * much more efficiently if you know the target architecture, but this is a
 * useful thought exercise. Note that GCC defines the handy constant __WORDSIZE
 * that tells us the size (in bits) of words on this architecture.
 */
void* wordwise_memset(void* s, int c, size_t sz) {
    uintptr_t* p = (uintptr_t*)s;
    uintptr_t x = c & 0xff;
    int i;
    int bytes_per_word;

    /* Construct a word's worth of the byte value we need to set. */
    for (i = 3; (1<<i) < __WORDSIZE; ++i)
        x |= x << (1<<i);
    /* At this point i is the power of 2 which is equal to the word size. */

    /* 1<<i would be the number of bits per word, therefore (1<<i)/8 == 1<<(i-3)
     * is the number of bytes per word.
     */
    bytes_per_word = 1<<(i-3);

    /* Check sz is bytes_per_word-byte-aligned. */
    assert(!(sz & (bytes_per_word-1)));
    sz >>= i-3;

    while (sz--)
        *p++ = x;
    return s;
}

/* Let's return to the 32-bit word-wise version briefly to implement a version
 * that handles unaligned (with respect to word size) pointers and size. To
 * understand what's going on here it's best to look at an example:
 *
 *  Calling wordwise_32_unaligned_memset(2, 0, 7)...
 * Initial:             +-+-+-+-+-+-+-+
 *                      |2|3|4|5|6|7|8|  pp = 2, p = ?
 *                      +-+-+-+-+-+-+-+  sz = 7, tail = ?
 *                      |?|?|?|?|?|?|?|
 *                      +-+-+-+-+-+-+-+
 *
 * After the prologue:  +-+-+-+-+-+-+-+ Now the pointer (pp/p) is aligned.
 *                      |2|3|4|5|6|7|8|  pp = 4, p = 4
 *                      +-+-+-+-+-+-+-+  sz = 5, tail = 1
 *                      |0|0|?|?|?|?|?| (then we adjust sz to 5>>2 == 1)
 *                      +-+-+-+-+-+-+-+
 *
 * After the main loop: +-+-+-+-+-+-+-+ We can't set any more word length
 *                      |2|3|4|5|6|7|8| regions, but there's still one byte
 *                      +-+-+-+-+-+-+-+ remaining.
 *                      |0|0|0|0|0|0|?|  pp = 8, p = 8
 *                      +-+-+-+-+-+-+-+  sz = -1, tail = 1
 *
 * After the epilogue:  +-+-+-+-+-+-+-+ Now we're done.
 *                      |2|3|4|5|6|7|8|  pp = 9, p = 8
 *                      +-+-+-+-+-+-+-+  sz = -1, tail = 0
 *                      |0|0|0|0|0|0|0|
 *                      +-+-+-+-+-+-+-+
 */
void* wordwise_32_unaligned_memset(void* s, int c, size_t sz) {
    uint32_t* p;
    uint32_t x = c & 0xff;
    byte xx = c & 0xff;
    byte* pp = (byte*)s;
    size_t tail;

    /* Let's introduce a prologue to bump the starting location forward to the
     * next alignment boundary.
     */
    while (((unsigned int)pp & 3) && sz--)
        *pp++ = xx;
    p = (uint32_t*)pp;

    /* Let's figure out the number of bytes that will be trailing when the
     * word-wise loop taps out.
     */
    tail = sz & 3;

    /* The middle of this function is identical to the wordwise_32_memset minus
     * the alignment checks.
     */
    x |= x << 8;
    x |= x << 16;

    sz >>= 2;

    while (sz--)
        *p++ = x;

    /* Now we introduce an epilogue to account for the trailing bytes. */
    pp = (byte*)p;
    while (tail--)
        *pp++ = xx;

    return s;
}

/* Let's take the final logical step and implement an architecture-independent
 * memset that can cope with unaligned pointers and sizes. Note, the same
 * caveat as for wordwise_memset applies; you wouldn't write code like this in
 * real life.
 */
void* wordwise_unaligned_memset(void* s, int c, size_t sz) {
    uintptr_t* p;
    uintptr_t x = c & 0xff;
    byte* pp = (byte*)s;
    byte xx = c & 0xff;
    size_t tail;
    int i;
    int bytes_per_word;

    for (i = 3; (1<<i) < __WORDSIZE; ++i)
        x |= x << (1<<i);
    bytes_per_word = 1<<(i-3);

    /* Prologue. */
    while (((unsigned int)pp & (bytes_per_word-1)) && sz--)
        *pp++ = xx;
    tail = sz & (bytes_per_word-1);
    p = (uintptr_t*)pp;

    /* Main loop. */
    sz >>= i-3;
    while (sz--)
        *p++ = x;

    /* Epilogue. */
    pp = (byte*)p;
    while (tail--)
        *pp++ = xx;

    return s;
}

/* Lines below here are instrumentation for testing your implementation. */

#define CHECK(f, unaligned) \
    do { \
        int fail_byte = check_memset((f), (unaligned)); \
        if (fail_byte) \
            printf("%s %s check failed on byte %d.\n", \
                   (unaligned) ? "Unaligned" : "Aligned", #f, fail_byte - 1); \
    } while(0)

#define BUFFER_LEN 4096

/* This function does some very basic checking of your memset function. If you
 * really want to comprehensively test your function you should check the
 * regions either side of the memory you are setting to make sure your function
 * is actually obeying the limits passed to it.
 *
 * Note, the argument unaligned determines whether we pass an unaligned pointer
 * and size or not. Only certain of the above implementations will pass this
 * test.
 */
int check_memset(void* f(), int unaligned) {
    char buffer[BUFFER_LEN];
    int i;
    char set;

    for (set = 0; (unsigned int)set <= 0xff; ++set) {
        f(buffer + unaligned, set, BUFFER_LEN - 2*unaligned);
        for (i = unaligned; i < BUFFER_LEN - 2*unaligned; ++i)
            if (buffer[i] != set)
                return i + 1;
    }

    return 0;
}

/* When executed, this program will just validate the implementations in this
 * file. Note that the unaligned tests are only run on the functions that can
 * cope with unaligned values.
 */
int main(int argc, char** argv) {

    /* Use GCC's built-in memset to validate our checking function. */
    CHECK(memset, 0);
    CHECK(memset, 1);

    /* Check our implementations. */
    CHECK(bytewise_memset, 0);
    CHECK(bytewise_memset, 1);
    CHECK(wordwise_32_memset, 0);
    CHECK(wordwise_memset, 0);
    CHECK(wordwise_32_unaligned_memset, 0);
    CHECK(wordwise_32_unaligned_memset, 1);
    CHECK(wordwise_unaligned_memset, 0);
    CHECK(wordwise_unaligned_memset, 1);

    return 0;
}
