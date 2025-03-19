EXTENSION = pg_sentence_transformer
MODULE_big = pg_sentence_transformer
OBJS = pg_sentence_transformer.o
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
PG_CFLAGS = -Wall -Wextra -Wno-declaration-after-statement -ggdb3
DATA = pg_sentence_transformer--0.1.sql

include $(PGXS)
