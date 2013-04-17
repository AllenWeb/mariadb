#ifndef YBT_H
#define YBT_H

#ident "Copyright (c) 2007 Tokutek Inc.  All rights reserved."

// brttypes.h must be first to make 64-bit file mode work right in linux.
#include "brttypes.h"
#include "../include/db.h"


DBT* toku_init_dbt (DBT *);
DBT *toku_fill_dbt(DBT *dbt, bytevec k, ITEMLEN len);
int toku_dbt_set (ITEMLEN len, bytevec val, DBT *d, struct simple_dbt *sdbt);
int toku_dbt_set_value (DBT *, bytevec *val, ITEMLEN vallen, void **staticptrp, BOOL ybt1_disposable);
void toku_sdbt_cleanup(struct simple_dbt *sdbt);

#endif
