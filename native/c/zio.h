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

#ifndef ZIO_H
#define ZIO_H

#if defined(__IBM_METAL__)
#define DCB_WRITE_MODEL(dcbwm)                                \
  __asm(                                                      \
      "*                                                  \n" \
      " DCB DDNAME=*-*,"                                      \
      "DSORG=PS,"                                             \
      "MACRF=W                                            \n" \
      "*                                                    " \
      : "DS"(dcbwm));
#else
#define DCB_WRITE_MODEL(dcbwm)
#endif

DCB_WRITE_MODEL(dcb_write_model);

#if defined(__IBM_METAL__)
#define DCB_READ_MODEL(dcbrm)                                 \
  __asm(                                                      \
      "*                                                  \n" \
      " DCB DDNAME=*-*,"                                      \
      "DSORG=PS,"                                             \
      "DCBE=*-*,"                                             \
      "MACRF=R                                            \n" \
      "*                                                    " \
      : "DS"(dcbrm));
#else
#define DCB_READ_MODEL(dcbrm)
#endif

DCB_READ_MODEL(dcb_read_model);

#if defined(__IBM_METAL__)
#define OPEN_MODEL(openm)                                     \
  __asm(                                                      \
      "*                                                  \n" \
      " OPEN (,),MODE=31,MF=L                             \n" \
      "*                                                    " \
      : "DS"(openm));
#else
#define OPEN_MODEL(openm)
#endif

OPEN_MODEL(open_model);

#if defined(__IBM_METAL__)
#define ACB_MODEL(acbm)                                       \
  __asm(                                                      \
      "*                                                  \n" \
      " ACB AM=VSAM                                       \n" \
      "*                                                    " \
      : "DS"(acbm));
#else
#define ACB_MODEL(acbm)
#endif

ACB_MODEL(acb_model);

#if defined(__IBM_METAL__)
#define RPL_MODEL(rplm)                                       \
  __asm(                                                      \
      "*                                                  \n" \
      " RPL AM=VSAM                                       \n" \
      "*                                                    " \
      : "DS"(rplm));
#else
#define RPL_MODEL(rplm)
#endif

RPL_MODEL(rpl_model);

#if defined(__IBM_METAL__)
#define GET(rpl, rc)                                 \
  __asm(                                             \
      "*                                         \n" \
      " GET RPL=(%1)                             \n" \
      " ST 15,%0     Save RC                     \n" \
      "*                                           " \
      : "=m"(rc)                                     \
      : "r"(rpl)                                     \
      : "r0", "r1", "r14", "r15");
#else
#define GET(rpl, rc)
#endif

#if defined(__IBM_METAL__)
#define POINT(rpl, rc)                               \
  __asm(                                             \
      "*                                         \n" \
      " POINT RPL=(%1)                           \n" \
      " ST 15,%0     Save RC                     \n" \
      "*                                           " \
      : "=m"(rc)                                     \
      : "r"(rpl)                                     \
      : "r0", "r1", "r14", "r15");
#else
#define POINT(rpl, rc)
#endif

#if defined(__IBM_METAL__)
#define MODCB_MODEL(modcbm)                                    \
  __asm(                                                       \
      "*                                                  \n"  \
      " MODCB MF=L                                        \n"  \
      "*                                                     " \
      : "DS"(modcbm));
#else
#define MODCB_MODEL(modcbm)
#endif

// NOTE(Kelosky): "OPTCD=(MVE,SYN,XRBA)," workable but conflicting documentation
// https://www.ibm.com/docs/en/zos/3.2.0?topic=examples-subparameters-gencb-modcb-showcb-testcb
#if defined(__IBM_METAL__)
#define MODCB(rpl, acb, area, area_len, rec_len, plist, rc, rsn) \
  __asm(                                                         \
      "*                                                 \n"     \
      " MODCB RPL=(*,%0),"                                       \
      "ACB=(*,%1),"                                              \
      "AREA=(*,%4),"                                             \
      "AREALEN=(*,%5),"                                          \
      "RECLEN=(*,%6),"                                           \
      "OPTCD=(MVE,SYN),"                                         \
      "MF=(G,%7)                                         \n"     \
      "*                                                 \n"     \
      " ST 15,%2     Save RC                             \n"     \
      " ST 0,%3      Save RSN                            \n"     \
      "*                                                 \n"     \
      : "+m"(rpl),                                               \
        "+m"(acb),                                               \
        "=m"(rc),                                                \
        "=m"(rsn)                                                \
      : "m"(area),                                               \
        "m"(area_len),                                           \
        "m"(rec_len),                                            \
        "m"(plist)                                               \
      : "r0", "r1", "r14", "r15");
#else
#define MODCB(rpl, acb, area, area_len, rec_len, plist, rc, rsn)
#endif

#if defined(__IBM_METAL__)
#define OPEN(dcb, plist, rc, mode)                            \
  __asm(                                                      \
      "*                                                  \n" \
      " OPEN (%0,(" #mode ")),"                               \
      "MODE=31,"                                              \
      "MF=(E,%2)                                          \n" \
      "*                                                  \n" \
      " ST    15,%1     Save RC                           \n" \
      "*                                                    " \
      : "+m"(dcb),                                            \
        "=m"(rc)                                              \
      : "m"(plist)                                            \
      : "r0", "r1", "r14", "r15");
#else
#define OPEN(dcb, plist, rc, mode)
#endif

#if defined(__IBM_METAL__)
#define OPEN_ACB(acb, plist, rc)                              \
  __asm(                                                      \
      "*                                                  \n" \
      " OPEN (%0),"                                           \
      "MODE=31,"                                              \
      "MF=(E,%2)                                          \n" \
      "*                                                  \n" \
      " ST    15,%1     Save RC                           \n" \
      "*                                                    " \
      : "+m"(acb),                                            \
        "=m"(rc)                                              \
      : "m"(plist)                                            \
      : "r0", "r1", "r14", "r15");
#else
#define OPEN_ACB(acb, plist, rc)
#endif

#if defined(__IBM_METAL__)
#define CLOSE_ACB(acb, plist, rc)                             \
  __asm(                                                      \
      "*                                                  \n" \
      "*                                                  \n" \
      " CLOSE (%0),"                                          \
      "MODE=31,"                                              \
      "MF=(E,%2)                                          \n" \
      "*                                                  \n" \
      " ST    15,%1     Save RC                           \n" \
      "*                                                    " \
      : "+m"(acb),                                            \
        "=m"(rc)                                              \
      : "m"(plist)                                            \
      : "r0", "r1", "r14", "r15");
#else
#define CLOSE_ACB(acb, plist, rc)
#endif

#if defined(__IBM_METAL__)
#define SYNADRLS()                                            \
  __asm(                                                      \
      "*                                                  \n" \
      " SYNADRLS                                          \n" \
      "*                                                  \n" \
      "*                                                    " \
      :                                                       \
      :                                                       \
      : "r0", "r1", "r14", "r15");
#else
#define SYNADRLS()
#endif

#if defined(__IBM_METAL__)
#define RDJFCB_MODEL(rdjfcbm)                                 \
  __asm(                                                      \
      "*                                                  \n" \
      " RDJFCB (,),"                                          \
      "MF=L                                               \n" \
      "*                                                    " \
      : "DS"(rdjfcbm));
#else
#define RDJFCB_MODEL(rdjfcbm)
#endif

RDJFCB_MODEL(rdfjfcb_model);

#if defined(__IBM_METAL__)
#define RDJFCB(dcb, plist, rc, mode)                          \
  __asm(                                                      \
      "*                                                  \n" \
      " RDJFCB (%0,(" #mode ")),"                             \
      "MF=(E,%2)                                          \n" \
      "*                                                  \n" \
      " ST    15,%1     Save RC                           \n" \
      "*                                                    " \
      : "+m"(dcb),                                            \
        "=m"(rc)                                              \
      : "m"(plist)                                            \
      : "r0", "r1", "r14", "r15");
#else
#define RDJFCB(dcb, plist, rc, mode)
#endif

#if defined(__IBM_METAL__)
#define FIND(dcb, member, rc, rsn)                            \
  __asm(                                                      \
      "*                                                  \n" \
      " FIND %0,"                                             \
      "%3,"                                                   \
      "D                                                  \n" \
      "*                                                  \n" \
      " ST    15,%1     Save RC                           \n" \
      " ST    0,%2     Save RSN                           \n" \
      "*                                                    " \
      : "+m"(dcb),                                            \
        "=m"(rc),                                             \
        "=m"(rsn)                                             \
      : "m"(member)                                           \
      : "r0", "r1", "r14", "r15");
#else
#define FIND(dcb, plist, rc, rsn)
#endif

// PDSE member is not connected
#if defined(__IBM_METAL__)
#define BLDL(dcb, list, rc, rsn)                              \
  __asm(                                                      \
      "*                                                  \n" \
      " BLDL %3,"                                             \
      "%0,"                                                   \
      "BYPASSLLA,"                                            \
      "NOCONNECT                                          \n" \
      "*                                                  \n" \
      " ST    15,%1     Save RC                           \n" \
      " ST    0,%2     Save RSN                           \n" \
      "*                                                    " \
      : "+m"(list),                                           \
        "=m"(rc),                                             \
        "=m"(rsn)                                             \
      : "m"(dcb)                                              \
      : "r0", "r1", "r14", "r15");
#else
#define BLDL(dcb, list, rc, rsn)
#endif

#if defined(__IBM_METAL__)
#define STOW(dcb, list, rc, rsn)                              \
  __asm(                                                      \
      "*                                                  \n" \
      " STOW %2,"                                             \
      "%3,"                                                   \
      "R                                                  \n" \
      "*                                                  \n" \
      " ST    15,%1     Save RC                           \n" \
      " ST    0,%2     Save RSN                           \n" \
      "*                                                    " \
      : "=m"(rc),                                             \
        "=m"(rsn)                                             \
      : "m"(dcb),                                             \
        "m"(list)                                             \
      : "r0", "r1", "r14", "r15");
#else
#define STOW(dcb, list, rc, rsn)
#endif

#if defined(__IBM_METAL__)
#define STOW_DELETE(dcb, list, rc, rsn)                       \
  __asm(                                                      \
      "*                                                  \n" \
      " STOW %2,"                                             \
      "%3,"                                                   \
      "D                                                  \n" \
      "*                                                  \n" \
      " ST    15,%0     Save RC                           \n" \
      " ST    0,%1      Save RSN                          \n" \
      "*                                                    " \
      : "=m"(rc),                                             \
        "=m"(rsn)                                             \
      : "m"(dcb),                                             \
        "m"(list)                                             \
      : "r0", "r1", "r14", "r15");
#else
#define STOW_DELETE(dcb, list, rc, rsn)
#endif

#if defined(__IBM_METAL__)
#define NOTE(dcb, listaddr, rc, rsn)                          \
  __asm(                                                      \
      "*                                                  \n" \
      " NOTE %3,"                                             \
      "REL                                                \n" \
      "*                                                  \n" \
      " ST    1,%0     Save result                        \n" \
      " ST    15,%1    Save RC                            \n" \
      " ST    0,%2     Save RSN                           \n" \
      "*                                                    " \
      : "=m"(listaddr),                                       \
        "=m"(rc),                                             \
        "=m"(rsn)                                             \
      : "m"(dcb)                                              \
      : "r0", "r1", "r14", "r15");
#else
#define NOTE(dcb, listaddr, rc, rsn)
#endif

#if defined(__IBM_METAL__)
#define CLOSE(dcb, plist, rc)                                 \
  __asm(                                                      \
      "*                                                  \n" \
      " CLOSE (%0),"                                          \
      "MODE=31,"                                              \
      "MF=(E,%2)                                          \n" \
      "*                                                  \n" \
      " ST    15,%1     Save RC                           \n" \
      "*                                                    " \
      : "+m"(dcb),                                            \
        "=m"(rc)                                              \
      : "m"(plist)                                            \
      : "r0", "r1", "r14", "r15");
#else
#define CLOSE(dcb, plist, rc)
#endif

#if defined(__IBM_METAL__)
#define WRITE(dcb, ecb, buf, rc)                              \
  __asm(                                                      \
      "*                                                  \n" \
      " WRITE %0,"                                            \
      "SF,"                                                   \
      "%2,"                                                   \
      "%3,"                                                   \
      "MF=E                                               \n" \
      "*                                                  \n" \
      " ST    15,%1     Save RC                           \n" \
      "*                                                    " \
      : "+m"(ecb),                                            \
        "=m"(*rc)                                             \
      : "m"(dcb),                                             \
        "m"(buf)                                              \
      : "r0", "r1", "r14", "r15");
#else
#define WRITE(dcb, ecb, buf, rc)
#endif

#if defined(__IBM_METAL__)
#define READ(dcb, ecb, buf, rc)                               \
  __asm(                                                      \
      "*                                                  \n" \
      " READ %0,"                                             \
      "SF,"                                                   \
      "%2,"                                                   \
      "%3,"                                                   \
      "'S',"                                                  \
      "MF=E                                               \n" \
      "*                                                  \n" \
      " ST    15,%1     Save RC                           \n" \
      "*                                                    " \
      : "+m"(ecb),                                            \
        "=m"(rc)                                              \
      : "m"(dcb),                                             \
        "m"(buf)                                              \
      : "r0", "r1", "r14", "r15");
#else
#define READ(dcb, ecb, buf, rc)
#endif

#if defined(__IBM_METAL__)
#define SNAP(dcb, header, start, end, plist, rc)              \
  __asm(                                                      \
      "*                                                  \n" \
      " SNAP DCB=%1,"                                         \
      "ID=1,"                                                 \
      "STORAGE=(%2,%3),"                                      \
      "STRHDR=%4,"                                            \
      "MF=(E,%5)                                          \n" \
      "*                                                  \n" \
      " ST    15,%0     Save RC                           \n" \
      "*                                                    " \
      : "=m"(rc)                                              \
      : "m"(dcb),                                             \
        "m"(start),                                           \
        "m"(end),                                             \
        "m"(header),                                          \
        "m"(plist)                                            \
      : "r0", "r1", "r14", "r15");
#else
#define SNAP(dcb, header, start, end, plist, rc)
#endif

#if defined(__IBM_METAL__)
#define CHECK(ecb, rc)                                        \
  __asm(                                                      \
      "*                                                  \n" \
      " CHECK %1                                          \n" \
      "*                                                  \n" \
      " ST    15,%0     Save RC                           \n" \
      "*                                                    " \
      : "=m"(rc)                                              \
      : "m"(ecb)                                              \
      : "r0", "r1", "r14", "r15");
#else
#define CHECK(ecb, rc)
#endif

#endif
