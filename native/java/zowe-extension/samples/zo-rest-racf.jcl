//ZORESTAC JOB (ACCT),'ZO-REST RACF',CLASS=A,MSGCLASS=H,NOTIFY=&SYSUID
//*--------------------------------------------------------------------
//* zo-rest (Zowe API ML extension): RACF definitions.
//*
//* Review, then submit under a user with RACF SPECIAL (or equivalent
//* authority over the FACILITY and APPL classes). Safe to resubmit:
//* RDEFINE of an existing profile fails with a harmless IRR message
//* (RC 4) and PERMIT can be repeated.
//*
//* Placeholder names (JCL symbols are NOT substituted inside in-stream
//* data, so edit the literal names in SYSTSIN below before submitting):
//*   ZWESVUSR  Zowe started task user (zowe.setup.security.users.zowe)
//*   ZWEINST   user that runs 'zwe components install'; needs READ on
//*             BPX.FILEATTR.PROGCTL to run 'extattr +p'
//*   ZOENDGRP  RACF group of end users for the optional certificate and
//*             APPL grants
//*   ZORSTUSR  user for the optional separate started task (section 8)
//*   ZORSTC    started task proc (samples/ZORSTC.jcl)
//*   ZWEADMIN  Zowe group; ZORSTUSR default group
//*   ZWESVUSR/ZOWEKRNG  the Zowe server's key ring
//*--------------------------------------------------------------------
//RACF     EXEC PGM=IKJEFT01,REGION=0M
//SYSTSPRT DD SYSOUT=*
//SYSTSIN  DD *
 /* ---- 1. FACILITY is active and RACLISTed on any normal system;
         nothing to change here.                                     */

 /* ---- 2. BPX.SERVER: lets the service user create per-user
         security environments (no password) for APIML JWT requests.
         UPDATE is required. Zowe's ZWESECUR normally grants this.
         WARNING: do NOT define BPX.SERVER here if it does not exist.
         Defining it switches every z/OS UNIX server on the system
         from superuser rules to profile-based authorization. If the
         RLIST at the end shows no profile, involve your security
         team before defining it.                                     */
 PERMIT BPX.SERVER CLASS(FACILITY) ID(ZWESVUSR) ACCESS(UPDATE)

 /* ---- 3. BPX.FILEATTR.PROGCTL: lets the installer run 'extattr +p'
         on lib/*.so (program control). Only needed when installing as
         a non-superuser; skip if a security admin runs extattr.      */
 RDEFINE FACILITY BPX.FILEATTR.PROGCTL UACC(NONE)
 PERMIT BPX.FILEATTR.PROGCTL CLASS(FACILITY) ID(ZWEINST) ACCESS(READ)

 /* ---- 4. Certificate and keyring endpoints (optional). Same
         checks as RACDCERT / R_datalib, evaluated against the end
         user. Grant only to users who manage their own certificates.
         IRR.DIGTCERT.LIST     list own certificates (READ)
         IRR.DIGTCERT.LISTRING list own key rings (READ)
         Write operations (ADD, GENCERT, ...) need further
         IRR.DIGTCERT.* profiles; add them per your site policy.      */
 RDEFINE FACILITY IRR.DIGTCERT.LIST UACC(NONE)
 RDEFINE FACILITY IRR.DIGTCERT.LISTRING UACC(NONE)
 PERMIT IRR.DIGTCERT.LIST CLASS(FACILITY) ID(ZOENDGRP) ACCESS(READ)
 PERMIT IRR.DIGTCERT.LISTRING CLASS(FACILITY) ID(ZOENDGRP) ACCESS(READ)

 /* ---- 5. Optional APPL gate (NOT yet wired in the extension).
         For zowe.commons.security.saf.applid. Left commented out:
         activating the APPL class is a system-wide decision.
 RDEFINE APPL ZOREST UACC(NONE)
 PERMIT ZOREST CLASS(APPL) ID(ZOENDGRP) ACCESS(READ)
                                                                      */

 /* ---- 6. Activate the changes ----------------------------------- */
 SETROPTS RACLIST(FACILITY) REFRESH

 /* ---- 7. Verify: expect ZWESVUSR UPDATE on BPX.SERVER and ZWEINST
         READ on BPX.FILEATTR.PROGCTL                                 */
 RLIST FACILITY BPX.SERVER AUTHUSER
 RLIST FACILITY BPX.FILEATTR.PROGCTL AUTHUSER
 RLIST FACILITY IRR.DIGTCERT.LIST AUTHUSER
 RLIST FACILITY IRR.DIGTCERT.LISTRING AUTHUSER

 /* ==== Running zo-rest as its own started task (optional) ==========
         Everything below applies only if you run zo-rest from the
         ZORSTC proc instead of (or next to) the Zowe launcher.
         Edit the UID and HOME literals before submitting.            */

 /* ---- 8. STC user: no password (protected), OMVS segment ----- */
 ADDUSER ZORSTUSR NOPASSWORD DFLTGRP(ZWEADMIN) NAME('ZO REST SERVER') +
   OMVS(UID(<uid>) HOME('/u/zorest') PROGRAM('/bin/sh'))

 /* ---- 9. Map the proc to the user (STARTED class) ------- */
 RDEFINE STARTED ZORSTC.* UACC(NONE) +
   STDATA(USER(ZORSTUSR) GROUP(ZWEADMIN) TRUSTED(NO))
 SETROPTS RACLIST(STARTED) REFRESH

 /* ---- 10. BPX.SERVER UPDATE (same warning as section 2: do NOT
         define BPX.SERVER if it does not exist) and BPX.JOBNAME
         (lets the server set its own job name).                      */
 PERMIT BPX.SERVER CLASS(FACILITY) ID(ZORSTUSR) ACCESS(UPDATE)
 RDEFINE FACILITY BPX.JOBNAME UACC(NONE)
 PERMIT BPX.JOBNAME CLASS(FACILITY) ID(ZORSTUSR) ACCESS(READ)
 SETROPTS RACLIST(FACILITY) REFRESH

 /* ---- 11. TCP/IP stack access: only if your site protects it with
         SERVAUTH EZB.STACKACCESS.* profiles. Use your site's profile
         name.
 PERMIT EZB.STACKACCESS.<sysname>.<stack> CLASS(SERVAUTH) +
   ID(ZORSTUSR) ACCESS(READ)
 SETROPTS RACLIST(SERVAUTH) REFRESH
                                                                      */

 /* ---- 12. Certificate access, option (a): read the Zowe server's
         key ring including the private key. RDATALIB must be active
         and RACLISTed. WARNING: defining the ring profile changes how
         access to that ring is checked (RDATALIB then governs it),
         so confirm ZWESVUSR keeps access before refreshing.
 RDEFINE RDATALIB ZWESVUSR.ZOWEKRNG.LST UACC(NONE)
 PERMIT ZWESVUSR.ZOWEKRNG.LST CLASS(RDATALIB) ID(ZORSTUSR) +
   ACCESS(CONTROL)
 PERMIT ZWESVUSR.ZOWEKRNG.LST CLASS(RDATALIB) ID(ZWESVUSR) +
   ACCESS(CONTROL)
 SETROPTS RACLIST(RDATALIB) REFRESH
         Option (b): create a separate ring for ZORSTUSR holding a
         certificate signed by the Zowe CA (RACDCERT ... GENCERT /
         ADDRING / CONNECT), and point the env file at it.
         PKCS12 file installs: instead give ZORSTUSR group read via
         ZWEADMIN (chmod 640 server.p12 and truststore.p12, 750 on
         their directory).                                            */
/*
//*--------------------------------------------------------------------
//* Removal (only for dedicated profiles):
//*   PERMIT <profile> CLASS(FACILITY) ID(<id>) DELETE
//*   RDELETE FACILITY <profile>   (never BPX.SERVER: Zowe shares it)
//* TCP/IP port reservation is not RACF; see the README.
//*--------------------------------------------------------------------
