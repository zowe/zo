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
//*--------------------------------------------------------------------
//RACF     EXEC PGM=IKJEFT01,REGION=0M
//SYSTSPRT DD SYSOUT=*
//SYSTSIN  DD *
 /* ---- 1. FACILITY class must be active and RACLISTed ------------ */
 SETROPTS CLASSACT(FACILITY) RACLIST(FACILITY)

 /* ---- 2. BPX.SERVER: lets the service user create per-user
         security environments (no password) for APIML JWT requests.
         UPDATE is required. Zowe's ZWESECUR normally defines this.   */
 RDEFINE FACILITY BPX.SERVER UACC(NONE)
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
         For zowe.commons.security.saf.applid. Define it only if you
         plan to enable the gate once the extension supports it.      */
 SETROPTS CLASSACT(APPL) RACLIST(APPL)
 RDEFINE APPL ZOREST UACC(NONE)
 PERMIT ZOREST CLASS(APPL) ID(ZOENDGRP) ACCESS(READ)

 /* ---- 6. Activate the changes ----------------------------------- */
 SETROPTS RACLIST(FACILITY APPL) REFRESH

 /* ---- 7. Verify: expect ZWESVUSR UPDATE on BPX.SERVER and ZWEINST
         READ on BPX.FILEATTR.PROGCTL                                 */
 RLIST FACILITY BPX.SERVER AUTHUSER
 RLIST FACILITY BPX.FILEATTR.PROGCTL AUTHUSER
 RLIST FACILITY IRR.DIGTCERT.LIST AUTHUSER
 RLIST FACILITY IRR.DIGTCERT.LISTRING AUTHUSER
 RLIST APPL ZOREST AUTHUSER
/*
//*--------------------------------------------------------------------
//* Removal (only for dedicated profiles):
//*   PERMIT <profile> CLASS(FACILITY) ID(<id>) DELETE
//*   RDELETE FACILITY <profile>   (never BPX.SERVER: Zowe shares it)
//* TCP/IP port reservation is not RACF; see the README.
//*--------------------------------------------------------------------
