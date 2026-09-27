#ifndef CODUOMP_BASE64_H
#define CODUOMP_BASE64_H

/* NOT_FROM_ORIGINAL_SOURCE (whole file): standard RFC 4648 base64 codec used
 * only by the master-branch remote-screenshot feature to carry binary JPEG
 * bytes over the text-only reliable command channel. Header-only so both the
 * client engine and the server game module can include it without adding a
 * shared translation unit to either build's explicit source manifest.
 *
 * Ownership and bounds: the caller owns both buffers. Encode requires
 * outCapacity >= coduomp_Base64EncodedLength(inputLength) + 1 (NUL). Decode
 * requires outCapacity >= coduomp_Base64DecodedLength(encoded) and returns
 * the decoded byte count, or -1 for malformed input (wrong length, or a byte
 * outside the alphabet/padding set); it never writes past outCapacity. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char coduomp_base64Alphabet[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static inline size_t coduomp_Base64EncodedLength(size_t inputLength)
{
    return ((inputLength + 2) / 3) * 4;
}

/* Encodes inputLength bytes from input into out, NUL-terminated. Returns the
 * number of encoded characters written (excluding the NUL), or (size_t)-1 if
 * outCapacity is too small. */
static inline size_t coduomp_Base64Encode(const uint8_t *input,
                                          size_t inputLength,
                                          char *out, size_t outCapacity)
{
    const size_t encodedLength = coduomp_Base64EncodedLength(inputLength);
    if (outCapacity < encodedLength + 1) {
        return (size_t)-1;
    }

    size_t outIndex = 0;
    size_t i = 0;
    for (; i + 3 <= inputLength; i += 3) {
        const uint32_t triple = ((uint32_t)input[i] << 16) |
                                 ((uint32_t)input[i + 1] << 8) |
                                 (uint32_t)input[i + 2];
        out[outIndex++] = coduomp_base64Alphabet[(triple >> 18) & 0x3F];
        out[outIndex++] = coduomp_base64Alphabet[(triple >> 12) & 0x3F];
        out[outIndex++] = coduomp_base64Alphabet[(triple >> 6) & 0x3F];
        out[outIndex++] = coduomp_base64Alphabet[triple & 0x3F];
    }

    const size_t remaining = inputLength - i;
    if (remaining == 1) {
        const uint32_t triple = (uint32_t)input[i] << 16;
        out[outIndex++] = coduomp_base64Alphabet[(triple >> 18) & 0x3F];
        out[outIndex++] = coduomp_base64Alphabet[(triple >> 12) & 0x3F];
        out[outIndex++] = '=';
        out[outIndex++] = '=';
    } else if (remaining == 2) {
        const uint32_t triple = ((uint32_t)input[i] << 16) |
                                 ((uint32_t)input[i + 1] << 8);
        out[outIndex++] = coduomp_base64Alphabet[(triple >> 18) & 0x3F];
        out[outIndex++] = coduomp_base64Alphabet[(triple >> 12) & 0x3F];
        out[outIndex++] = coduomp_base64Alphabet[(triple >> 6) & 0x3F];
        out[outIndex++] = '=';
    }

    out[outIndex] = '\0';
    return outIndex;
}

static inline int coduomp_Base64DecodeValue(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Decodes a NUL-terminated base64 string (as produced by
 * coduomp_Base64Encode) into out. Returns the number of decoded bytes, or -1
 * if encoded is malformed or would overflow outCapacity. */
static inline int32_t coduomp_Base64Decode(const char *encoded,
                                           uint8_t *out, size_t outCapacity)
{
    const size_t encodedLength = strlen(encoded);
    if (encodedLength == 0 || (encodedLength % 4) != 0) {
        return -1;
    }

    size_t paddingBytes = 0;
    if (encodedLength >= 2 && encoded[encodedLength - 1] == '=') {
        ++paddingBytes;
        if (encoded[encodedLength - 2] == '=') {
            ++paddingBytes;
        }
    }

    const size_t maxDecodedLength =
        (encodedLength / 4) * 3 - paddingBytes;
    if (outCapacity < maxDecodedLength) {
        return -1;
    }

    size_t outIndex = 0;
    for (size_t i = 0; i < encodedLength; i += 4) {
        int values[4];
        int groupPadding = 0;

        for (int j = 0; j < 4; ++j) {
            const char c = encoded[i + j];
            if (c == '=') {
                values[j] = 0;
                ++groupPadding;
            } else {
                values[j] = coduomp_Base64DecodeValue(c);
                if (values[j] < 0) {
                    return -1;
                }
            }
        }

        const uint32_t triple = ((uint32_t)values[0] << 18) |
                                 ((uint32_t)values[1] << 12) |
                                 ((uint32_t)values[2] << 6) |
                                 (uint32_t)values[3];

        if (outIndex < outCapacity) {
            out[outIndex++] = (uint8_t)((triple >> 16) & 0xFF);
        }
        if (groupPadding < 2 && outIndex < outCapacity) {
            out[outIndex++] = (uint8_t)((triple >> 8) & 0xFF);
        }
        if (groupPadding < 1 && outIndex < outCapacity) {
            out[outIndex++] = (uint8_t)(triple & 0xFF);
        }
    }

    return (int32_t)outIndex;
}

#endif /* CODUOMP_BASE64_H */
