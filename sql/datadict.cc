/* Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "datadict.h"
#include "sql_priv.h"
#include "sql_class.h"
#include "sql_table.h"


/**
  Check type of .frm if we are not going to parse it.

  @param[in]  thd   The current session.
  @param[in]  path  path to FRM file.
  @param[out] dbt   db_type of the table if FRMTYPE_TABLE, otherwise undefined.

  @retval  FRMTYPE_ERROR        error
  @retval  FRMTYPE_TABLE        table
  @retval  FRMTYPE_VIEW         view
*/

frm_type_enum dd_frm_type(THD *thd, char *path, enum legacy_db_type *dbt)
{
  File file;
  uchar header[10];     //"TYPE=VIEW\n" it is 10 characters
  size_t error;
  DBUG_ENTER("dd_frm_type");

  *dbt= DB_TYPE_UNKNOWN;

  if ((file= mysql_file_open(key_file_frm, path, O_RDONLY | O_SHARE, MYF(0))) < 0)
    DBUG_RETURN(FRMTYPE_ERROR);
  error= mysql_file_read(file, (uchar*) header, sizeof(header), MYF(MY_NABP));
  mysql_file_close(file, MYF(MY_WME));

  if (error)
    DBUG_RETURN(FRMTYPE_ERROR);
  if (!strncmp((char*) header, "TYPE=VIEW\n", sizeof(header)))
    DBUG_RETURN(FRMTYPE_VIEW);

  /*
    This is just a check for DB_TYPE. We'll return default unknown type
    if the following test is true (arg #3). This should not have effect
    on return value from this function (default FRMTYPE_TABLE)
  */
  if (!is_binary_frm_header(header))
    DBUG_RETURN(FRMTYPE_TABLE);

  /*
    XXX this is a bug.
    if header[3] is > DB_TYPE_FIRST_DYNAMIC, then the complete
    storage engine name must be read from the frm
  */
  *dbt= (enum legacy_db_type) (uint) *(header + 3);

  /* Probably a table. */
  DBUG_RETURN(FRMTYPE_TABLE);
}


/**
  Given a table name, check type of .frm and legacy table type.

  @param[in]   thd          The current session.
  @param[in]   db           Table schema.
  @param[in]   table_name   Table database.
  @param[out]  table_type   handlerton of the table if FRMTYPE_TABLE,
                            otherwise undefined.

  @return FALSE if FRMTYPE_TABLE and storage engine found. TRUE otherwise.
*/

bool dd_frm_storage_engine(THD *thd, const char *db, const char *table_name,
                           handlerton **table_type)
{
  char path[FN_REFLEN + 1];
  enum legacy_db_type db_type;
  LEX_STRING db_name = {(char *) db, strlen(db)};

  /* There should be at least some lock on the table.  */
  DBUG_ASSERT(thd->mdl_context.is_lock_owner(MDL_key::TABLE, db,
                                             table_name, MDL_SHARED));

  if (check_db_name(&db_name))
  {
    my_error(ER_WRONG_DB_NAME, MYF(0), db_name.str);
    return TRUE;
  }

  if (check_table_name(table_name, strlen(table_name), FALSE))
  {
    my_error(ER_WRONG_TABLE_NAME, MYF(0), table_name);
    return TRUE;
  }

  (void) build_table_filename(path, sizeof(path) - 1, db,
                              table_name, reg_ext, 0);

  dd_frm_type(thd, path, &db_type);

  /* Type is unknown if the object is not found or is not a table. */
  if (db_type == DB_TYPE_UNKNOWN ||
      !(*table_type= ha_resolve_by_legacy_type(thd, db_type)))
  {
    my_error(ER_NO_SUCH_TABLE, MYF(0), db, table_name);
    return TRUE;
  }

  return FALSE;
}


/**
  Given a table name, check if the storage engine for the
  table referred by this name supports an option 'flag'.
  Return an error if the table does not exist or is not a
  base table.

  @pre Any metadata lock on the table.

  @param[in]    thd         The current session.
  @param[in]    db          Table schema.
  @param[in]    table_name  Table database.
  @param[in]    flag        The option to check.
  @param[out]   yes_no      The result. Undefined if error.
*/

bool dd_check_storage_engine_flag(THD *thd,
                                  const char *db, const char *table_name,
                                  uint32 flag, bool *yes_no)
{
  handlerton *table_type;

  if (dd_frm_storage_engine(thd, db, table_name, &table_type))
    return TRUE;

  *yes_no= ha_check_storage_engine_flag(table_type, flag);

  return FALSE;
}


/*
  Regenerate a metadata locked table.

  @param  thd   Thread context.
  @param  db    Name of the database to which the table belongs to.
  @param  name  Table name.
  @param  path  For temporary tables only - path to table files.
                Otherwise NULL (the path is calculated from db and table names).

  @retval  FALSE  Success.
  @retval  TRUE   Error.
*/

bool dd_recreate_table(THD *thd, const char *db, const char *table_name,
                       const char *path)
{
  bool error= TRUE;
  HA_CREATE_INFO create_info;
  char path_buf[FN_REFLEN + 1];
  DBUG_ENTER("dd_recreate_table");

  memset(&create_info, 0, sizeof(create_info));

  if (path)
    create_info.options|= HA_LEX_CREATE_TMP_TABLE;
  else
  {
    build_table_filename(path_buf, sizeof(path_buf) - 1,
                         db, table_name, "", 0);
    path= path_buf;

    /* There should be a exclusive metadata lock on the table. */
    DBUG_ASSERT(thd->mdl_context.is_lock_owner(MDL_key::TABLE, db, table_name,
                                               MDL_EXCLUSIVE));
  }

  /* Attempt to reconstruct the table. */
  error= ha_create_table(thd, path, db, table_name, &create_info);

  DBUG_RETURN(error);
}

