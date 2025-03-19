build:
	make

clean:
	make clean

install: build
	make install

pyinstall:
	pip install .
