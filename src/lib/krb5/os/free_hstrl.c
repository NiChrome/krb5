/*
 * lib/krb5/os/free_hstrl.c
 *
 * Copyright 1990 by the Massachusetts Institute of Technology.
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
 * 
 *
 * krb5_free_host_realm()
 */

#include "k5-int.h"
#include <stdio.h>

/*
 Frees the storage taken by a realm list returned by krb5_get_host_realm.
 */

KRB5_DLLIMP krb5_error_code KRB5_CALLCONV
krb5_free_host_realm(context, realmlist)
    krb5_context context;
    char FAR * const FAR *realmlist;
{
    /* same format, so why duplicate code? */
    return krb5_free_krbhst(context, realmlist);
}
