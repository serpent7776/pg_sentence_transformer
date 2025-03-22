#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "storage/shmem.h"
#include "storage/lwlock.h"
#include "utils/builtins.h"
#include "executor/spi.h"
#include "postmaster/bgworker.h"
#include "utils/snapmgr.h"
#include "utils/memutils.h"
#include "utils/backend_status.h"
#include "utils/guc.h"
#pragma GCC diagnostic pop

PG_MODULE_MAGIC;

PGDLLEXPORT void _PG_init(void);
PGDLLEXPORT void pg_sentence_transformer_main(Datum main_arg);

char *TransformerVenvPath = "";
char *TransformerDatabaseName = "postgres";
char *TransformerSchemaName = "";
char *TransformerSrcTableName = "posts";
char *TransformerSrcColumnName = "body";
char *TransformeridColumnName = "id";

void _PG_init(void)
{
	if (!process_shared_preload_libraries_in_progress)
		ereport(ERROR, (errmsg("pg_sentence_transformer must be loaded via shared_preload_libraries")));

	DefineCustomStringVariable(
		"sentence_transformer.venv_path",
		gettext_noop("Path to the python virtual env with sentence_transformers installed"),
		NULL,
		&TransformerVenvPath,
		"",
		PGC_POSTMASTER,
		GUC_SUPERUSER_ONLY,
		NULL, NULL, NULL);
	DefineCustomStringVariable(
		"sentence_transformer.database_name",
		gettext_noop("Database which pg_sentence_transformer should connect to."),
		NULL,
		&TransformerDatabaseName,
		"postgres",
		PGC_POSTMASTER,
		GUC_SUPERUSER_ONLY,
		NULL, NULL, NULL);
	DefineCustomStringVariable(
		"sentence_transformer.schema_name",
		gettext_noop("Schema name."),
		NULL,
		&TransformerSchemaName,
		"",
		PGC_POSTMASTER,
		GUC_SUPERUSER_ONLY,
		NULL, NULL, NULL);
	DefineCustomStringVariable(
		"sentence_transformer.src_table_name",
		gettext_noop("Table containing data to transform"),
		NULL,
		&TransformerSrcTableName,
		"posts",
		PGC_POSTMASTER,
		GUC_SUPERUSER_ONLY,
		NULL, NULL, NULL);
	DefineCustomStringVariable(
		"sentence_transformer.src_column",
		gettext_noop("Column with data to transform"),
		NULL,
		&TransformerSrcColumnName,
		"body",
		PGC_POSTMASTER,
		GUC_SUPERUSER_ONLY,
		NULL, NULL, NULL);
	DefineCustomStringVariable(
		"sentence_transformer.id_column",
		gettext_noop("Unique identifier column of the table"),
		NULL,
		&TransformeridColumnName,
		"id",
		PGC_POSTMASTER,
		GUC_SUPERUSER_ONLY,
		NULL, NULL, NULL);

	BackgroundWorker worker = {
		.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION,
		.bgw_start_time = BgWorkerStart_RecoveryFinished,
		.bgw_restart_time = 10,
		.bgw_notify_pid = 0,
	};

	snprintf(worker.bgw_name, BGW_MAXLEN, "sentence_transformer");
	snprintf(worker.bgw_library_name, BGW_MAXLEN, "pg_sentence_transformer");
	snprintf(worker.bgw_function_name, BGW_MAXLEN, "pg_sentence_transformer_main");

	RegisterBackgroundWorker(&worker);
}

void pg_sentence_transformer_main(Datum main_arg)
{
	BackgroundWorkerInitializeConnection(TransformerDatabaseName, NULL, 0);

	BackgroundWorkerUnblockSignals();

	CurrentResourceOwner = ResourceOwnerCreate(NULL, "pg_sentence_transformer");

	MyBackendType = B_BACKEND;
	pgstat_report_appname("pg_sentence_transformer");
	pgstat_report_activity(STATE_IDLE, NULL);

	StartTransactionCommand();
	PushActiveSnapshot(GetTransactionSnapshot());
	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR,
			(errcode(ERRCODE_INTERNAL_ERROR),
			 errmsg("could not connect to SPI manager"),
			 errdetail("SPI_connect failed")));
	}

	if (strcmp(TransformerVenvPath, "") != 0)
	{
		const char* sql = "CALL sentence_transformer.activate_python_venv($1)";
		pgstat_report_activity(STATE_RUNNING, sql);
		Oid argTypes[1] = {TEXTOID};
		SPIPlanPtr plan = SPI_prepare(sql, 1, argTypes);
		if (plan == NULL)
		{
			ereport(ERROR, (errmsg("Failed to prepare plan")));
		}
		Datum argValues[1] = {
			CStringGetTextDatum(TransformerVenvPath),
		};
		if (SPI_execute_plan(plan, argValues, NULL, false, 0) != SPI_OK_UTILITY)
		{
			ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("Failed to switch to venv")));
		}
	}

	{
		const char* sql = "CALL sentence_transformer.prepare($1, $2, $3, $4)";
		pgstat_report_activity(STATE_RUNNING, sql);
		Oid argTypes[4] = {TEXTOID, TEXTOID, TEXTOID, TEXTOID};
		SPIPlanPtr plan = SPI_prepare(sql, 4, argTypes);
		if (plan == NULL)
		{
			ereport(ERROR, (errmsg("Failed to prepare plan")));
		}
		Datum argValues[4] = {
			CStringGetTextDatum(TransformerSchemaName),
			CStringGetTextDatum(TransformerSrcTableName),
			CStringGetTextDatum(TransformeridColumnName),
			CStringGetTextDatum(TransformerSrcColumnName),
		};
		if (SPI_execute_plan(plan, argValues, NULL, false, 0) != SPI_OK_UTILITY)
		{
			ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("Failed to prepare work")));
		}
	}

	SPI_finish();
	PopActiveSnapshot();
	CommitTransactionCommand();

	while (true)
	{
		StartTransactionCommand();
		PushActiveSnapshot(GetTransactionSnapshot());
		if (SPI_connect() != SPI_OK_CONNECT)
		{
			ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("could not connect to SPI manager"),
				 errdetail("SPI_connect failed")));
		}

		const char* sql = "CALL sentence_transformer.process($1, $2, $3, $4)";
		pgstat_report_activity(STATE_RUNNING, sql);
		Oid argTypes[4] = {TEXTOID, TEXTOID, TEXTOID, TEXTOID};
		SPIPlanPtr plan = SPI_prepare(sql, 4, argTypes);
		if (plan == NULL)
		{
			ereport(ERROR, (errmsg("Failed to prepare plan")));
		}
		Datum argValues[4] = {
			CStringGetTextDatum(TransformerSchemaName),
			CStringGetTextDatum(TransformerSrcTableName),
			CStringGetTextDatum(TransformeridColumnName),
			CStringGetTextDatum(TransformerSrcColumnName),
		};
		if (SPI_execute_plan(plan, argValues, NULL, false, 0) != SPI_OK_UTILITY)
		{
			ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("Failed to insert initial data to the queue")));
		}

		pgstat_report_activity(STATE_IDLE, NULL);
		SPI_finish();
		PopActiveSnapshot();
		CommitTransactionCommand();
	}

	proc_exit(0);
}
