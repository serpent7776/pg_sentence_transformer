# pg_sentence_transformer

Run a huggingface model right inside Postgres process, without the need for an external LLM service.

## Installation

The usual make invocation will install the extension.

```
sudo make install
```

This extension also requires `pg_sentence_transformer` python package to be installed in venv readable by `postgres` user.

```
sudo -u postgres -i
mkdir /opt/pg_sentence_transformer
virtualenv /opt/pg_sentence_transformer/venv
. /opt/pg_sentence_transformer/venv/bin/activate
cd /path/to/pg_sentence_transformer
pip install .
```

This extension needs to be added to `shared_preload_libraries` in your `postgresql.conf`:

```
shared_preload_libraries = 'pg_sentence_transformer'
```

## Configuration

The following configuration options are recognised and should be set in `postgresql.conf`:

```
sentence_transformer.venv_path = '/opt/pg_sentence_transformer/venv'
sentence_transformer.database_name = my_db
sentence_transformer.schema_name = public
sentence_transformer.src_table_name = posts
sentence_transformer.src_column = text
sentence_transformer.id_column = id
```

```
sentence_transformer.venv_path
```

Path to python virtual environment where `pg_sentence_transformer` python package and its dependencies were installed.
Virtual environment needs to be created with `virtualenv` tool.
Note that this path must be readable by `postges` user.

```
sentence_transformer.database_name
```

The database name to which the worker should connect to.

```
sentence_transformer.schema_name
```

Schema name that contains the source table. The destination table with embeddings will also be created in this schema.

```
sentence_transformer.src_table_name
```

Source table name with the data for which the embeddings should be computed.
The embeddings will be stored in the newly created table. It's name will be the same as the source table, but will `_embeddings` suffix.

```
sentence_transformer.src_column
```

Column name in the source table containing the source text for which the embeddings should be computed.

```
sentence_transformer.id_column
```

The integral ID column in the source table that uniquely identifies the row.
