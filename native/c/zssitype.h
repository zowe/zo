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

#ifndef ZSSITYPE_H
#define ZSSITYPE_H

#include "iefjssob.h"
#include "iefjssib.h"
#include "iazssst.h"
#include "iazssji.h"
#include "iazsplio.h"
#include "iazssjm.h"
#include "iazssjp.h"
#include "iazjproc.h"

typedef struct ssob SSOB;
typedef struct ssib SSIB;
typedef struct stat STAT;

typedef struct statjq STATJQ;
typedef struct statjqhd STATJQHD;
typedef struct statjqtr STATJQTR;
typedef struct statve STATVE;
typedef struct statjvhd STATJVHD;
typedef struct statjqvb STATJQVB;

typedef struct statse STATSE;
typedef struct statsehd STATSEHD;
typedef struct statsetr STATSETR;
typedef struct statsj2t STATSJ2T;

typedef struct statvo STATVO;
typedef struct statsvhd STATSVHD;
typedef struct statsevb STATSEVB;
typedef struct statseo2 STATSEO2;

typedef struct iazssji IAZSSJI;
typedef struct splio SPLIO;

typedef struct ssjm SSJM;
typedef struct ssjf SSJF;

typedef struct jproc JPROC;
typedef struct jpshdr JPSHDR;
typedef struct jprcstor JPRCSTOR;
typedef struct jprhdr JPRHDR;
typedef struct jprpref JPRPREF;
typedef struct jprgeni JPRGENI;
typedef struct jprdsets JPRDSETS;
typedef struct jprdsinf JPRDSINF;

typedef struct ssjp SSJP;
#endif
