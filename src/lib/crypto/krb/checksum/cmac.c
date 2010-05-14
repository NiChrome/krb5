/*
 * lib/crypto/krb/checksum/cmac.c
 *
 * Copyright 2010 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 */
/*
 * Portions Copyright (C) The Internet Society (2006).
 *
 * This document is subject to the rights, licenses and restrictions
 * contained in BCP 78, and except as set forth therein, the authors
 * retain all their rights.
 *
 * This document and the information contained herein are provided on an
 * "AS IS" basis and THE CONTRIBUTOR, THE ORGANIZATION HE/SHE REPRESENTS
 * OR IS SPONSORED BY (IF ANY), THE INTERNET SOCIETY AND THE INTERNET
 * ENGINEERING TASK FORCE DISCLAIM ALL WARRANTIES, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO ANY WARRANTY THAT THE USE OF THE
 * INFORMATION HEREIN WILL NOT INFRINGE ANY RIGHTS OR ANY IMPLIED
 * WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "k5-int.h"
#include "etypes.h"
#include "aead.h"
#include "etypes.h"
#include "cksumtypes.h"

#define BLOCK_SIZE 16

static unsigned char const_Rb[BLOCK_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87
};

static unsigned char const_Zero[BLOCK_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void
xor_128(unsigned char *a, unsigned char *b, unsigned char *out)
{
    int z;

    for (z = 0; z < BLOCK_SIZE / 4; z++) {
        unsigned char *aptr = &a[z * 4];
        unsigned char *bptr = &b[z * 4];
        unsigned char *outptr = &out[z * 4];

        store_32_n(load_32_n(aptr) ^ load_32_n(bptr), outptr);
    }
}

static void
leftshift_onebit(unsigned char *input, unsigned char *output)
{
    int i;
    unsigned char overflow = 0;

    for (i = BLOCK_SIZE - 1; i >= 0; i--) {
        output[i] = input[i] << 1;
        output[i] |= overflow;
        overflow = (input[i] & 0x80) ? 1 : 0;
    }
}

static krb5_error_code
generate_subkey(const struct krb5_enc_provider *enc,
                krb5_key key,
                unsigned char *K1,
                unsigned char *K2)
{
    unsigned char Z[BLOCK_SIZE];
    unsigned char L[BLOCK_SIZE];
    unsigned char tmp[BLOCK_SIZE];
    krb5_crypto_iov iov[1];
    krb5_data d;
    krb5_error_code ret;

    memset(Z, 0, sizeof(Z));
    iov[0].flags = KRB5_CRYPTO_TYPE_DATA;
    iov[0].data = make_data((char *)Z, sizeof(Z));

    d = make_data((char *)L, BLOCK_SIZE);

    /*
     * CBC in terms of CBC-MAC; at the cost of an additional XOR,
     * this avoids needing to extend the SPI interface (because we
     * need both the CBC-MAC function from the CCM provider and
     * the CBC function from the CTS provider).
     */
    ret = enc->cbc_mac(key, iov, 1, NULL, &d);
    if (ret != 0)
        return ret;

    xor_128(const_Zero, L, L);

    if ((L[0] & 0x80) == 0) {
        leftshift_onebit(L, K1);
    } else {
        leftshift_onebit(L, tmp);
        xor_128(tmp, const_Rb, K1);
    }

    if ((K1[0] & 0x80) == 0) {
        leftshift_onebit(K1, K2);
    } else {
        leftshift_onebit(K1, tmp);
        xor_128(tmp, const_Rb, K2);
    }

    return 0;
}

static void
padding(unsigned char *lastb, unsigned char *pad, int length)
{
    int j;

    /* original last block */
    for (j = 0; j < BLOCK_SIZE; j++) {
        if (j < length) {
            pad[j] = lastb[j];
        } else if (j == length) {
            pad[j] = 0x80;
        } else {
            pad[j] = 0x00;
        }
    }
}

/*
 * Implementation of CMAC algorithm. When used with AES, this function
 * is compatible with RFC 4493.
 */
krb5_error_code
krb5int_cmac_checksum(const struct krb5_cksumtypes *ctp,
                      krb5_key key, krb5_keyusage usage,
                      const krb5_crypto_iov *data, size_t num_data,
                      krb5_data *output)
{
    const struct krb5_enc_provider *enc = ctp->enc;
    unsigned char Y[BLOCK_SIZE], M_last[BLOCK_SIZE], padded[BLOCK_SIZE];
    unsigned char K1[BLOCK_SIZE], K2[BLOCK_SIZE];
    unsigned char input[BLOCK_SIZE];
    unsigned int n, i, flag;
    krb5_error_code ret;
    struct iov_block_state iov_state;
    unsigned int length;
    krb5_data ivec;
    krb5_crypto_iov iov[1];
    krb5_data d;

    assert(enc->cbc_mac != NULL);

    if (enc->block_size != BLOCK_SIZE)
        return KRB5_BAD_MSIZE;

    for (i = 0, length = 0; i < num_data; i++) {
        const krb5_crypto_iov *piov = &data[i];

        if (SIGN_IOV(piov))
            length += piov->data.length;
    }

    ret = generate_subkey(enc, key, K1, K2);
    if (ret != 0)
        return ret;

    n = (length + BLOCK_SIZE - 1) / BLOCK_SIZE;

    if (n == 0) {
        n = 1;
        flag = 0;
    } else {
        flag = ((length % BLOCK_SIZE) == 0);
    }

    iov[0].flags = KRB5_CRYPTO_TYPE_DATA;
    iov[0].data = make_data((char *)input, BLOCK_SIZE);

    memset(Y, 0, BLOCK_SIZE);

    ivec = make_data((char *)Y, BLOCK_SIZE);
    d = make_data((char *)Y, BLOCK_SIZE);

    IOV_BLOCK_STATE_INIT(&iov_state);
    iov_state.include_sign_only = 1;

    for (i = 0; i < n - 1; i++) {
        krb5int_c_iov_get_block(input, BLOCK_SIZE, data, num_data, &iov_state);

        ret = enc->cbc_mac(key, iov, 1, &ivec, &d);
        if (ret != 0)
            return ret;
    }

    krb5int_c_iov_get_block(input, BLOCK_SIZE, data, num_data, &iov_state);

    if (flag) {
        /* last block is complete block */
        xor_128(input, K1, M_last);
    } else {
        padding(input, padded, length % BLOCK_SIZE);
        xor_128(padded, K2, M_last);
    }

    iov[0].data = make_data((char *)M_last, BLOCK_SIZE);

    ret = enc->cbc_mac(key, iov, 1, &ivec, &d);
    if (ret != 0)
        return ret;

    assert(output->length >= d.length);

    output->length = d.length;
    memcpy(output->data, d.data, d.length);

    return ret;
}

