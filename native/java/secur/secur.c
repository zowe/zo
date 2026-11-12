/**
 * This program and the accompanying materials are made available under the terms of the
 * Eclipse Public License v2.0 which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v20.html
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * Copyright Contributors to the Zowe Project.
 *
 */

#define _OPEN_SYS 1
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include "secur.h"
#include "jnitools.h"

#define PSATOLD 0x21C
#define TCBSTCB 0x138
#define STCBOTCB 0x0D8
#define OTCBTHLI 0x0BC

#define THLIAPPLIDLEN 0x052
#define THLIAPPLID 0x070

int lastErrno2 = 0;

/*
https://www.ibm.com/support/knowledgecenter/en/SSLTBW_2.3.0/com.ibm.zos.v2r3.bpxbd00/ptsec.htm
*/

JNIEXPORT jint JNICALL Java_org_zowe_zo_zos_security_jni_Secur_createSecurityEnvironmentByDaemon(JNIEnv *env, jobject obj, jstring user, jstring applId)
{
    int rc = EINVAL;
    char *platformUser = jstring_to_ebcdic(env, user);
    char *platformApplId = jstring_to_ebcdic(env, applId);
    if (platformUser != NULL)
    {
        int userLength = strlen(platformUser);
        if (pthread_security_applid_np(__DAEMON_SECURITY_ENV, __USERID_IDENTITY, userLength, platformUser, NULL, 0, platformApplId) != 0) {
            rc = errno;
            lastErrno2 = __errno2();
        }
        else {
            rc = 0;
            lastErrno2 = 0;
        }
    }
    free_if_not_null(platformUser);
    free_if_not_null(platformApplId);
    return rc;
}

JNIEXPORT jint JNICALL Java_org_zowe_zo_zos_security_jni_Secur_createSecurityEnvironment(JNIEnv *env, jobject obj, jstring user, jstring password, jstring applId)
{
    int rc = EINVAL;
    char *platformUser = jstring_to_ebcdic(env, user);
    char *platformPassword = jstring_to_ebcdic(env, password);
    char *platformApplId = jstring_to_ebcdic(env, applId);
    if (platformUser != NULL)
    {
        int userLength = strlen(platformUser);
        if (pthread_security_applid_np(__CREATE_SECURITY_ENV, __USERID_IDENTITY, userLength, platformUser, platformPassword, 0, platformApplId) != 0) {
            rc = errno;
            lastErrno2 = __errno2();
        }
        else {
            rc = 0;
            lastErrno2 = 0;
        }
    }
    free_if_not_null(platformUser);
    free_if_not_null(platformApplId);
    free_if_not_null(platformPassword);
    return rc;
}

JNIEXPORT jint JNICALL Java_org_zowe_zo_zos_security_jni_Secur_removeSecurityEnvironment(JNIEnv *env, jobject obj)
{
    return pthread_security_applid_np(__DELETE_SECURITY_ENV, __USERID_IDENTITY, 0, NULL, NULL, 0, NULL);
}

JNIEXPORT jint JNICALL Java_org_zowe_zo_zos_security_jni_Secur_getLastErrno2(JNIEnv *env, jobject obj)
{
    return lastErrno2;
}

JNIEXPORT jint JNICALL Java_org_zowe_zo_zos_security_jni_Secur_setApplid(JNIEnv *env, jobject obj, jstring jApplid)
{
    void *__ptr32 psa = 0;
    void *__ptr32 tcb = *(void *__ptr32 *)(psa + PSATOLD);
    void *__ptr32 stcb = *(void *__ptr32 *)(tcb + TCBSTCB);
    void *__ptr32 otcb = *(void *__ptr32 *)(stcb + STCBOTCB);
    void *__ptr32 thli = *(void *__ptr32 *)(otcb + OTCBTHLI);

    if (memcmp("THLI", thli, 4) != 0)
    {
        printf("Could not set APPLID: BPXYTHLI control block not found\n");
        return -1;
    }

    char *origApplid = (char *)malloc(9);
    char *applid = jstring_to_ebcdic(env, jApplid);
    if (applid == NULL) {
            return -1;  // conversion failed
    }
    const int applidLength = strlen(applid);
    if (applidLength > 8) {
        free_if_not_null(applid);
        return -1;  // reject anything that won't fit the THLI field
    }
    char *__ptr32 thliApplidLen = (char *__ptr32)(thli + THLIAPPLIDLEN);
    *thliApplidLen = applidLength;

    void *__ptr32 thliApplid = (void *__ptr32)(thli + THLIAPPLID);
    memset(thliApplid, ' ', 8);
    origApplid[8] = 0;
    memcpy(origApplid, thliApplid, 8);
    memcpy(thliApplid, applid, applidLength);
    free_if_not_null(applid);

    /* A call to pthread_security_np causes that the value set above is correctly propagated */
    pthread_security_np(0, 0, 0, NULL, NULL, 0);
    errno = 0;
    return 0;
}
