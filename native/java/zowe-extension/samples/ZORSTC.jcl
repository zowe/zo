//*--------------------------------------------------------------------
//* ZORSTC: started task PROC that runs zo-rest outside the Zowe
//* launcher, under its own security ID.
//*
//* Install:
//*   - Copy this member to a PROCLIB (member name ZORSTC).
//*   - Edit INST to the install directory (placeholder /u/zorest).
//*     Layout:  <INST>/runtime        unpaxed zo-rest component
//*              <INST>/instance/zo.env  settings (see
//*              config/standalone.env.example)
//*   - zo.env must be owned by the STC user and be mode 600.
//*   - Assign the proc an STC user (ZORSTUSR); see the security
//*     samples zo-rest-racf.jcl, zo-rest-tss.txt, zo-rest-acf2.txt.
//*
//* Operate:  S ZORSTC to start. P (STOP) does not reach the JVM
//*           under BPXBATSL. To stop gracefully (SIGTERM), find the
//*           java pid with D OMVS,U=ZORSTUSR, then issue
//*           F BPXOINIT,TERM=<pid>. C ZORSTC is the last resort.
//*
//* The PARM is limited to 100 characters AFTER symbol expansion, so
//* keep INST short (or use a symlink). The env file argument is
//* relative to the component root; an absolute path also works.
//*--------------------------------------------------------------------
//ZORSTC   PROC INST='/u/zorest'
//ZORSTC   EXEC PGM=BPXBATSL,REGION=0M,TIME=NOLIMIT,
//            PARM='PGM /bin/sh &INST./runtime/bin/run-standalone.sh ..
//             /instance/zo.env'
//STDOUT   DD SYSOUT=*
//STDERR   DD SYSOUT=*
