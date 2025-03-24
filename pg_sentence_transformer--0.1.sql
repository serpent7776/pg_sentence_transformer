CREATE SCHEMA sentence_transformer;

CREATE TABLE sentence_transformer.queue(
    id BIGINT PRIMARY KEY
);

-- https://www.enterprisedb.com/blog/using-virtual-environment-plpython3-postgresql
CREATE OR REPLACE PROCEDURE sentence_transformer.activate_python_venv(venv TEXT)
AS
$BODY$
    import os
    import sys

    try:
        if sys.platform in ('win32', 'win64', 'cygwin'):
            activate_this = os.path.join(venv, 'Scripts', 'activate_this.py')
        else:
            activate_this = os.path.join(venv, 'bin', 'activate_this.py')

        exec(open(activate_this).read(), dict(__file__=activate_this))
    except e:
        plpy.error(e)
        raise e
$BODY$
LANGUAGE plpython3u;

CREATE OR REPLACE PROCEDURE sentence_transformer.prepare(schema TEXT, text_table TEXT, id_col TEXT, src_col TEXT)
AS
$$
    import os

    from pg_sentence_transformer import model # preload model
    import nltk

    nltk.download('punkt')
    nltk.download('punkt_tab')

    r = plpy.execute(f"""
        CREATE TABLE IF NOT EXISTS {schema}.{text_table}_embeddings(ref_id BIGINT PRIMARY KEY, embedding vector(384) NOT NULL)
        """)

    r = plpy.execute(f"""
        CREATE INDEX IF NOT EXISTS {text_table}_embeddings_idx ON {schema}.{text_table}_embeddings USING ivfflat(embedding)
        """)

    r = plpy.execute(f"""
        CREATE OR REPLACE FUNCTION {schema}.pg_sentence_transformer_{text_table}_enqueue_proc()
        RETURNS TRIGGER LANGUAGE plpgsql
        AS $f$
        BEGIN
            INSERT INTO sentence_transformer.queue(id)
            VALUES (NEW.{id_col});
            RETURN NULL;
        END;
        $f$
        """)

    r = plpy.execute(f"""
        CREATE OR REPLACE TRIGGER {schema}_{text_table}_insert_trigger
        AFTER INSERT OR UPDATE OF {src_col} ON {schema}.{text_table}
        FOR EACH ROW EXECUTE FUNCTION {schema}.pg_sentence_transformer_{text_table}_enqueue_proc()
        """)

    r = plpy.execute(f"""
        CREATE OR REPLACE FUNCTION {schema}.pg_sentence_transformer_{text_table}_delete_proc()
        RETURNS TRIGGER LANGUAGE plpgsql
        AS $f$
        BEGIN
            DELETE FROM {schema}.{text_table}_embeddings
            WHERE ref_id = OLD.{id_col};
            RETURN NULL;
        END;
        $f$
        """)

    r = plpy.execute(f"""
        CREATE OR REPLACE TRIGGER {schema}_{text_table}_delete_trigger
        AFTER DELETE ON {schema}.{text_table}
        FOR EACH ROW EXECUTE FUNCTION {schema}.pg_sentence_transformer_{text_table}_delete_proc()
        """)

    r = plpy.execute(f"""
        INSERT INTO sentence_transformer.queue(id)
        SELECT {id_col} FROM {schema}.{text_table} AS s
        WHERE NOT EXISTS (select 1 from {schema}.{text_table}_embeddings AS e where e.ref_id=s.{id_col})
        ON CONFLICT (id) DO NOTHING
        """)
$$
LANGUAGE plpython3u;

CREATE OR REPLACE PROCEDURE sentence_transformer.process(schema TEXT, text_table TEXT, id_col TEXT, src_col TEXT)
AS $$
    import plpy
    import time

    from pg_sentence_transformer import model
    from nltk import sent_tokenize

    with plpy.subtransaction():
        sql = "SELECT id FROM sentence_transformer.queue LIMIT 1 FOR UPDATE SKIP LOCKED"
        r = plpy.execute(sql)
        if len(r) > 0:
            id = r[0]['id']

            sql = f"SELECT {src_col} AS text FROM {schema}.{text_table} WHERE {id_col} = $1"
            plan = plpy.prepare(sql, ["bigint"])
            r = plpy.execute(plan, [id])
            if len(r) > 0:
                text = r[0]['text']
                sentences = sent_tokenize(text)
                embeddings = [model.sbert_model.encode(sentence) for sentence in sentences]

                for e in embeddings:
                    sql = f"""
                    INSERT INTO {schema}.{text_table}_embeddings(ref_id, embedding)
                    values($1, $2)
                    ON CONFLICT (ref_id) DO UPDATE SET embedding = excluded.embedding
                    """
                    plan = plpy.prepare(sql, ['bigint', 'vector(384)'])
                    es = e.tolist()
                    vec = f"[{','.join(str(x) for x in es)}]"
                    r = plpy.execute(plan, [id, vec])

                sql = "DELETE FROM sentence_transformer.queue WHERE id=$1"
                plan = plpy.prepare(sql, ['bigint'])
                plpy.execute(plan, [id])
    time.sleep(0.1)
$$ LANGUAGE plpython3u;
